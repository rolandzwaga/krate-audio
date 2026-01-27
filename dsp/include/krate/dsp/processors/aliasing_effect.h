// ==============================================================================
// Layer 2: DSP Processor - AliasingEffect
// ==============================================================================
// Intentional aliasing processor with band isolation and frequency shifting.
// Creates digital grunge/lo-fi aesthetic by downsampling without anti-aliasing,
// causing high frequencies to fold back into the audible spectrum.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (RAII, value semantics, C++20)
// - Principle IX: Layer 2 (depends only on Layer 0 and Layer 1, composes Layer 2)
// - Principle X: DSP Constraints (parameter smoothing, denormal flushing)
// - Principle XII: Test-First Development
//
// Reference: specs/112-aliasing-effect/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/primitives/biquad.h>
#include <krate/dsp/primitives/sample_rate_reducer.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/processors/frequency_shifter.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace Krate {
namespace DSP {

/// @brief Layer 2 DSP Processor - Intentional aliasing with band isolation
///
/// Creates digital grunge/lo-fi aesthetic by downsampling without anti-aliasing,
/// causing high frequencies to fold back into the audible spectrum. Features
/// configurable band isolation and pre-downsample frequency shifting.
///
/// @par Algorithm
/// 1. Band isolation: Separate input into band and non-band components
/// 2. Frequency shift: Apply SSB modulation to shift band content
/// 3. Downsample: Sample-and-hold without anti-aliasing (creates aliasing)
/// 4. Recombine: Sum non-band signal with aliased band signal
/// 5. Mix: Blend with dry input
///
/// @par Features
/// - Configurable downsample factor [2, 32] for mild to extreme aliasing
/// - Frequency shift [-5000, +5000] Hz before downsample affects aliasing patterns
/// - Band isolation [20Hz, Nyquist] with 24dB/oct slopes
/// - Click-free parameter automation via 10ms smoothing
/// - Mono processing only (instantiate two for stereo)
///
/// @par Real-Time Safety
/// All processing methods are noexcept and allocation-free after prepare().
/// Safe for audio callbacks.
///
/// @par Thread Safety
/// Not thread-safe. Create separate instances per audio channel.
///
/// @par Latency
/// Approximately 5 samples from internal frequency shifter (Hilbert transform).
/// Not compensated in output.
///
/// @par Usage
/// @code
/// AliasingEffect aliaser;
/// aliaser.prepare(44100.0, 512);
/// aliaser.setDownsampleFactor(8.0f);
/// aliaser.setAliasingBand(2000.0f, 8000.0f);
/// aliaser.setFrequencyShift(500.0f);
/// aliaser.setMix(0.75f);
///
/// for (size_t i = 0; i < numSamples; ++i) {
///     output[i] = aliaser.process(input[i]);
/// }
/// @endcode
class AliasingEffect {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kMinDownsampleFactor = 2.0f;
    static constexpr float kMaxDownsampleFactor = 32.0f;
    static constexpr float kDefaultDownsampleFactor = 2.0f;

    static constexpr float kMinFrequencyShiftHz = -5000.0f;
    static constexpr float kMaxFrequencyShiftHz = 5000.0f;

    static constexpr float kMinBandFrequencyHz = 20.0f;
    // Max band frequency is sampleRate * 0.45 (set dynamically)

    static constexpr float kSmoothingTimeMs = 10.0f;

    // =========================================================================
    // Lifecycle (FR-001, FR-002, FR-003)
    // =========================================================================

    /// @brief Default constructor
    ///
    /// Creates an unprepared processor. Call prepare() before processing.
    /// Processing before prepare() returns input unchanged.
    AliasingEffect() noexcept = default;

    /// @brief Destructor
    ~AliasingEffect() = default;

    // Non-copyable due to FrequencyShifter containing non-copyable components
    AliasingEffect(const AliasingEffect&) = delete;
    AliasingEffect& operator=(const AliasingEffect&) = delete;
    AliasingEffect(AliasingEffect&&) noexcept = default;
    AliasingEffect& operator=(AliasingEffect&&) noexcept = default;

    /// @brief Initialize for given sample rate (FR-001, FR-003)
    ///
    /// Prepares all internal components. Must be called before processing.
    /// Supports sample rates from 44100Hz to 192000Hz.
    ///
    /// @param sampleRate Sample rate in Hz
    /// @param maxBlockSize Maximum block size (for future buffer pre-allocation)
    /// @note NOT real-time safe (FrequencyShifter allocates internally)
    void prepare(double sampleRate, size_t maxBlockSize) noexcept {
        (void)maxBlockSize;  // Reserved for future use

        sampleRate_ = sampleRate;
        const auto sampleRateF = static_cast<float>(sampleRate);

        // Calculate max band frequency (45% of Nyquist)
        maxBandFrequencyHz_ = sampleRateF * 0.45f;

        // Initialize sample rate reducer
        reducer_.prepare(sampleRate);
        reducer_.setReductionFactor(downsampleFactor_);

        // Initialize frequency shifter with fixed configuration (FR-012a)
        shifter_.prepare(sampleRate);
        shifter_.setDirection(ShiftDirection::Up);
        shifter_.setFeedback(0.0f);
        shifter_.setModDepth(0.0f);
        shifter_.setMix(1.0f);
        shifter_.setShiftAmount(frequencyShiftHz_);

        // Initialize band filters (24dB/oct = 2-stage cascade)
        updateBandFilters();

        // Initialize smoothers
        downsampleSmoother_.configure(kSmoothingTimeMs, sampleRateF);
        downsampleSmoother_.snapTo(downsampleFactor_);

        shiftSmoother_.configure(kSmoothingTimeMs, sampleRateF);
        shiftSmoother_.snapTo(frequencyShiftHz_);

        bandLowSmoother_.configure(kSmoothingTimeMs, sampleRateF);
        bandLowSmoother_.snapTo(bandLowHz_);

        bandHighSmoother_.configure(kSmoothingTimeMs, sampleRateF);
        bandHighSmoother_.snapTo(bandHighHz_);

        mixSmoother_.configure(kSmoothingTimeMs, sampleRateF);
        mixSmoother_.snapTo(mix_);

        prepared_ = true;
    }

    /// @brief Clear all internal state without reallocation (FR-002)
    ///
    /// Resets all filters, shifter, reducer, and smoothers.
    /// Does not change parameter values or sample rate.
    void reset() noexcept {
        reducer_.reset();
        shifter_.reset();
        bandHighpassCascade_.reset();
        bandLowpassCascade_.reset();
        nonBandHighpassCascade_.reset();
        nonBandLowpassCascade_.reset();

        downsampleSmoother_.snapTo(downsampleFactor_);
        shiftSmoother_.snapTo(frequencyShiftHz_);
        bandLowSmoother_.snapTo(bandLowHz_);
        bandHighSmoother_.snapTo(bandHighHz_);
        mixSmoother_.snapTo(mix_);
    }

    // =========================================================================
    // Downsample Control (FR-004, FR-005, FR-006, FR-007)
    // =========================================================================

    /// @brief Set the downsample factor (FR-004, FR-005)
    ///
    /// Higher factors create more severe aliasing. No anti-aliasing filter
    /// is applied (FR-007), so all frequencies above reduced Nyquist fold back.
    ///
    /// @param factor Reduction factor, clamped to [2.0, 32.0]
    ///               2 = mild aliasing, 32 = extreme aliasing
    /// @note Change is smoothed over 10ms (FR-006)
    void setDownsampleFactor(float factor) noexcept {
        downsampleFactor_ = std::clamp(factor, kMinDownsampleFactor, kMaxDownsampleFactor);
        downsampleSmoother_.setTarget(downsampleFactor_);
    }

    /// @brief Get current downsample factor
    [[nodiscard]] float getDownsampleFactor() const noexcept {
        return downsampleFactor_;
    }

    // =========================================================================
    // Frequency Shift Control (FR-008, FR-009, FR-010, FR-011, FR-012, FR-012a)
    // =========================================================================

    /// @brief Set pre-downsample frequency shift (FR-008, FR-009)
    ///
    /// Shifts all frequencies by a constant Hz amount before downsampling.
    /// This affects which frequencies alias and where they fold to.
    /// Uses SSB modulation (FR-012) with fixed internal configuration (FR-012a).
    ///
    /// @param hz Shift amount in Hz, clamped to [-5000, +5000]
    ///           Positive = frequencies shift up, Negative = frequencies shift down
    /// @note Change is smoothed over 10ms (FR-010)
    /// @note Applied before downsampling (FR-011)
    void setFrequencyShift(float hz) noexcept {
        frequencyShiftHz_ = std::clamp(hz, kMinFrequencyShiftHz, kMaxFrequencyShiftHz);
        shiftSmoother_.setTarget(frequencyShiftHz_);
    }

    /// @brief Get current frequency shift in Hz
    [[nodiscard]] float getFrequencyShift() const noexcept {
        return frequencyShiftHz_;
    }

    // =========================================================================
    // Aliasing Band Control (FR-013, FR-014, FR-015, FR-016, FR-017, FR-018)
    // =========================================================================

    /// @brief Set the frequency band to apply aliasing to (FR-013)
    ///
    /// Only content within this band is processed through the aliaser.
    /// Content outside the band bypasses the aliaser and recombines after (FR-018).
    /// Band filter uses 24dB/oct slopes (FR-017).
    ///
    /// @param lowHz Low band frequency, clamped to [20, sampleRate*0.45] Hz (FR-014)
    /// @param highHz High band frequency, clamped to [20, sampleRate*0.45] Hz (FR-014)
    /// @note lowHz is constrained to be <= highHz (FR-015)
    /// @note Changes are smoothed over 10ms (FR-016)
    void setAliasingBand(float lowHz, float highHz) noexcept {
        // Clamp to valid range
        lowHz = std::clamp(lowHz, kMinBandFrequencyHz, maxBandFrequencyHz_);
        highHz = std::clamp(highHz, kMinBandFrequencyHz, maxBandFrequencyHz_);

        // Ensure low <= high (FR-015)
        if (lowHz > highHz) {
            lowHz = highHz;
        }

        bandLowHz_ = lowHz;
        bandHighHz_ = highHz;
        bandLowSmoother_.setTarget(bandLowHz_);
        bandHighSmoother_.setTarget(bandHighHz_);
    }

    /// @brief Get current aliasing band low frequency in Hz
    [[nodiscard]] float getAliasingBandLow() const noexcept {
        return bandLowHz_;
    }

    /// @brief Get current aliasing band high frequency in Hz
    [[nodiscard]] float getAliasingBandHigh() const noexcept {
        return bandHighHz_;
    }

    // =========================================================================
    // Mix Control (FR-019, FR-020, FR-021, FR-022)
    // =========================================================================

    /// @brief Set dry/wet mix (FR-019, FR-020)
    ///
    /// @param mix Mix amount, clamped to [0.0, 1.0]
    ///            0.0 = bypass (dry only), 1.0 = full wet
    /// @note Change is smoothed over 10ms (FR-021)
    /// @note Formula: output = (1-mix)*dry + mix*wet (FR-022)
    void setMix(float mix) noexcept {
        mix_ = std::clamp(mix, 0.0f, 1.0f);
        mixSmoother_.setTarget(mix_);
    }

    /// @brief Get current dry/wet mix
    [[nodiscard]] float getMix() const noexcept {
        return mix_;
    }

    // =========================================================================
    // Processing (FR-023, FR-024, FR-025, FR-026, FR-027, FR-028, FR-029, FR-030)
    // =========================================================================

    /// @brief Process a single sample (FR-023)
    ///
    /// Processing chain (FR-028):
    /// input -> band isolation -> frequency shift (FR-029) ->
    /// downsample (no AA) -> recombine with non-band (FR-030) -> mix with dry
    ///
    /// @param input Input sample
    /// @return Processed output sample
    ///
    /// @note Returns input unchanged if prepare() not called
    /// @note Returns 0 and resets on NaN/Inf input (FR-025)
    /// @note noexcept, allocation-free (FR-024)
    /// @note Output is bounded, no NaN/Inf output (FR-027)
    /// @note Flushes denormals (FR-026)
    [[nodiscard]] float process(float input) noexcept {
        if (!prepared_) {
            return input;
        }

        // Handle NaN/Inf input (FR-025)
        if (detail::isNaN(input) || detail::isInf(input)) {
            reset();
            return 0.0f;
        }

        // Store dry signal for mixing
        const float dry = input;

        // Update smoothed parameters
        const float smoothedDownsample = downsampleSmoother_.process();
        const float smoothedShift = shiftSmoother_.process();
        const float smoothedBandLow = bandLowSmoother_.process();
        const float smoothedBandHigh = bandHighSmoother_.process();
        const float smoothedMix = mixSmoother_.process();

        // Early return for mix=0 bypass (SC-007: bit-exact dry signal)
        if (smoothedMix < 0.0001f) {
            return dry;
        }

        // Update components if smoothers are active
        if (!downsampleSmoother_.isComplete()) {
            reducer_.setReductionFactor(smoothedDownsample);
        }
        if (!shiftSmoother_.isComplete()) {
            shifter_.setShiftAmount(smoothedShift);
        }
        if (!bandLowSmoother_.isComplete() || !bandHighSmoother_.isComplete()) {
            updateBandFiltersSmoothed(smoothedBandLow, smoothedBandHigh);
        }

        // -----------------------------------------------------------------
        // Processing Chain (FR-028)
        // -----------------------------------------------------------------

        // Step 1: Band isolation - extract band and non-band components
        // Band signal: HP at lowFreq, then LP at highFreq (bandpass)
        float bandSignal = bandHighpassCascade_.process(input);
        bandSignal = bandLowpassCascade_.process(bandSignal);

        // Non-band signal: LP at lowFreq OR HP at highFreq
        // We use complementary filtering: non-band = input - band
        // But for cleaner isolation, use explicit filters:
        // Low non-band: LP at lowFreq
        float lowNonBand = nonBandLowpassCascade_.process(input);
        // High non-band: HP at highFreq
        float highNonBand = nonBandHighpassCascade_.process(input);

        // Step 2: Frequency shift the band signal (FR-029)
        float shiftedBand = shifter_.process(bandSignal);

        // Step 3: Downsample the shifted band (no AA) (FR-007)
        float aliasedBand = reducer_.process(shiftedBand);

        // Step 4: Recombine aliased band with non-band components (FR-030)
        float wet = aliasedBand + lowNonBand + highNonBand;

        // Step 5: Mix with dry (FR-022)
        float output = (1.0f - smoothedMix) * dry + smoothedMix * wet;

        // Flush denormals (FR-026)
        output = detail::flushDenormal(output);

        return output;
    }

    /// @brief Process a buffer in-place (FR-023)
    ///
    /// @param buffer Audio buffer (modified in-place)
    /// @param numSamples Number of samples to process
    ///
    /// @note noexcept, allocation-free (FR-024)
    void process(float* buffer, size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] = process(buffer[i]);
        }
    }

    // =========================================================================
    // Query (FR-034)
    // =========================================================================

    /// @brief Check if processor has been prepared
    [[nodiscard]] bool isPrepared() const noexcept {
        return prepared_;
    }

    /// @brief Get processing latency in samples (FR-034)
    ///
    /// @return Approximately 5 samples (from internal frequency shifter)
    [[nodiscard]] static constexpr size_t getLatencySamples() noexcept {
        return 5;  // From FrequencyShifter's Hilbert transform
    }

private:
    // =========================================================================
    // Internal Methods
    // =========================================================================

    /// @brief Update band filter coefficients
    void updateBandFilters() noexcept {
        const auto sampleRateF = static_cast<float>(sampleRate_);

        // Band isolation filters (24dB/oct = 2-stage Butterworth cascade)
        // Bandpass = HP at low freq, then LP at high freq
        bandHighpassCascade_.setButterworth(FilterType::Highpass, bandLowHz_, sampleRateF);
        bandLowpassCascade_.setButterworth(FilterType::Lowpass, bandHighHz_, sampleRateF);

        // Non-band filters
        // Low non-band: everything below the band
        nonBandLowpassCascade_.setButterworth(FilterType::Lowpass, bandLowHz_, sampleRateF);
        // High non-band: everything above the band
        nonBandHighpassCascade_.setButterworth(FilterType::Highpass, bandHighHz_, sampleRateF);
    }

    /// @brief Update band filter coefficients with smoothed values
    void updateBandFiltersSmoothed(float lowHz, float highHz) noexcept {
        const auto sampleRateF = static_cast<float>(sampleRate_);

        bandHighpassCascade_.setButterworth(FilterType::Highpass, lowHz, sampleRateF);
        bandLowpassCascade_.setButterworth(FilterType::Lowpass, highHz, sampleRateF);
        nonBandLowpassCascade_.setButterworth(FilterType::Lowpass, lowHz, sampleRateF);
        nonBandHighpassCascade_.setButterworth(FilterType::Highpass, highHz, sampleRateF);
    }

    // =========================================================================
    // Member Variables
    // =========================================================================

    // Components
    SampleRateReducer reducer_;
    FrequencyShifter shifter_;
    BiquadCascade<2> bandHighpassCascade_;   // 24dB/oct highpass for band
    BiquadCascade<2> bandLowpassCascade_;    // 24dB/oct lowpass for band
    BiquadCascade<2> nonBandLowpassCascade_; // 24dB/oct lowpass for non-band low
    BiquadCascade<2> nonBandHighpassCascade_;// 24dB/oct highpass for non-band high

    // Parameter smoothers
    OnePoleSmoother downsampleSmoother_;
    OnePoleSmoother shiftSmoother_;
    OnePoleSmoother bandLowSmoother_;
    OnePoleSmoother bandHighSmoother_;
    OnePoleSmoother mixSmoother_;

    // Parameters (raw target values)
    float downsampleFactor_ = kDefaultDownsampleFactor;
    float frequencyShiftHz_ = 0.0f;
    float bandLowHz_ = kMinBandFrequencyHz;
    float bandHighHz_ = 20000.0f;
    float mix_ = 1.0f;

    // State
    double sampleRate_ = 44100.0;
    float maxBandFrequencyHz_ = 20000.0f;
    bool prepared_ = false;
};

} // namespace DSP
} // namespace Krate
