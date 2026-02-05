// ==============================================================================
// Layer 2: DSP Processor - Additive Synthesis Oscillator
// ==============================================================================
// IFFT-based additive synthesis oscillator implementing up to 128 sinusoidal
// partials. Uses overlap-add resynthesis with Hann windowing at 75% overlap
// for efficient O(N log N) synthesis independent of partial count.
//
// Features:
// - Per-partial amplitude, frequency ratio, and phase control
// - Spectral tilt (dB/octave brightness control)
// - Piano-string inharmonicity for bell/metallic timbres
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (process/setters: noexcept, no alloc)
// - Principle III: Modern C++ (C++20, [[nodiscard]], value semantics)
// - Principle IX: Layer 2 (depends on Layer 0-1 only)
// - Principle XII: Test-First Development
//
// Reference: specs/025-additive-oscillator/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/primitives/fft.h>
#include <krate/dsp/core/window_functions.h>
#include <krate/dsp/core/phase_utils.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/db_utils.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Krate {
namespace DSP {

// =============================================================================
// AdditiveOscillator Class
// =============================================================================

/// @brief IFFT-based additive synthesis oscillator.
///
/// Generates sound by summing up to 128 sinusoidal partials, with efficient
/// IFFT overlap-add processing. Provides per-partial control and macro
/// parameters for spectral tilt and inharmonicity.
///
/// @par Layer: 2 (processors/)
/// @par Dependencies: primitives/fft.h, core/window_functions.h, core/phase_utils.h
///
/// @par Memory Model
/// All buffers allocated in prepare(). Processing is allocation-free.
///
/// @par Thread Safety
/// Single-threaded. All methods must be called from the same thread.
///
/// @par Real-Time Safety
/// - prepare(): NOT real-time safe (allocates memory)
/// - All other methods: Real-time safe (noexcept, no allocations)
class AdditiveOscillator {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// Maximum number of partials supported
    static constexpr size_t kMaxPartials = 128;

    /// Minimum supported FFT size
    static constexpr size_t kMinFFTSize = 512;

    /// Maximum supported FFT size
    static constexpr size_t kMaxFFTSize = 4096;

    /// Default FFT size
    static constexpr size_t kDefaultFFTSize = 2048;

    /// Minimum fundamental frequency
    static constexpr float kMinFundamental = 0.1f;

    /// Minimum spectral tilt (dB/octave)
    static constexpr float kMinSpectralTilt = -24.0f;

    /// Maximum spectral tilt (dB/octave)
    static constexpr float kMaxSpectralTilt = 12.0f;

    /// Maximum inharmonicity coefficient
    static constexpr float kMaxInharmonicity = 0.1f;

    /// Maximum frequency ratio for partials
    static constexpr float kMaxFrequencyRatio = 64.0f;

    /// Minimum frequency ratio (for clamping invalid values)
    static constexpr float kMinFrequencyRatio = 0.001f;

    // =========================================================================
    // Lifecycle (FR-001, FR-002, FR-003)
    // =========================================================================

    /// @brief Default constructor.
    ///
    /// Initializes to default state:
    /// - fundamental = 440 Hz
    /// - numPartials = 1
    /// - spectralTilt = 0 dB/octave
    /// - inharmonicity = 0
    /// - partial 1 amplitude = 1.0, others = 0.0
    /// - unprepared state (processBlock() outputs zeros)
    AdditiveOscillator() noexcept {
        initializeDefaultState();
    }

    /// @brief Destructor.
    ~AdditiveOscillator() noexcept = default;

    // Non-copyable, movable
    AdditiveOscillator(const AdditiveOscillator&) = delete;
    AdditiveOscillator& operator=(const AdditiveOscillator&) = delete;
    AdditiveOscillator(AdditiveOscillator&&) noexcept = default;
    AdditiveOscillator& operator=(AdditiveOscillator&&) noexcept = default;

    /// @brief Initialize for processing at given sample rate (FR-001, FR-002).
    ///
    /// @param sampleRate Sample rate in Hz (44100-192000)
    /// @param fftSize FFT size (512, 1024, 2048, or 4096). Default: 2048.
    ///
    /// @pre fftSize is power of 2 in [512, 4096]
    /// @post isPrepared() returns true
    ///
    /// @note NOT real-time safe (allocates memory)
    void prepare(double sampleRate, size_t fftSize = kDefaultFFTSize) noexcept {
        // Validate and clamp FFT size
        if (fftSize < kMinFFTSize) fftSize = kMinFFTSize;
        if (fftSize > kMaxFFTSize) fftSize = kMaxFFTSize;

        // Ensure power of 2
        if ((fftSize & (fftSize - 1)) != 0) {
            fftSize = kDefaultFFTSize;
        }

        sampleRate_ = sampleRate;
        fftSize_ = fftSize;
        hopSize_ = fftSize / 4;  // 75% overlap for COLA
        numBins_ = fftSize / 2 + 1;
        nyquist_ = static_cast<float>(sampleRate / 2.0);

        // Prepare FFT
        fft_.prepare(fftSize);

        // Allocate buffers
        spectrum_.resize(numBins_);
        ifftBuffer_.resize(fftSize);
        window_.resize(fftSize);
        outputBuffer_.resize(fftSize * 2);  // Double-buffered

        // Generate Hann window (FR-019, FR-020)
        Window::generateHann(window_.data(), fftSize);

        // Initialize state
        reset();

        prepared_ = true;
    }

    /// @brief Reset internal state without changing configuration (FR-003).
    ///
    /// Clears phase accumulators and output buffer. Configuration (fundamental,
    /// partials, tilt, inharmonicity) is preserved. Phase values set via
    /// setPartialPhase() take effect here.
    ///
    /// @note Real-time safe
    void reset() noexcept {
        // Copy initial phases to accumulated phases (FR-011)
        for (size_t i = 0; i < kMaxPartials; ++i) {
            accumulatedPhases_[i] = static_cast<double>(partialInitialPhases_[i]);
        }

        // Clear output buffer
        std::fill(outputBuffer_.begin(), outputBuffer_.end(), 0.0f);
        outputWriteIndex_ = 0;
        outputReadIndex_ = 0;
        samplesInBuffer_ = 0;
        framesGenerated_ = 0;
    }

    // =========================================================================
    // Fundamental Frequency (FR-005, FR-006, FR-007)
    // =========================================================================

    /// @brief Set the fundamental frequency for all partials (FR-005).
    ///
    /// @param hz Frequency in Hz, clamped to [0.1, sampleRate/2)
    ///        Setting to 0 or below minimum produces silence (FR-007)
    ///
    /// @note NaN and Infinity inputs are sanitized to safe defaults
    /// @note Real-time safe
    void setFundamental(float hz) noexcept {
        // Sanitize NaN/Inf
        if (detail::isNaN(hz) || detail::isInf(hz)) {
            fundamental_ = 0.0f;  // Will produce silence
            return;
        }

        // Clamp to valid range (FR-006)
        // Values below kMinFundamental are stored as-is for silence check (FR-007)
        if (hz < 0.0f) {
            fundamental_ = 0.0f;
        } else if (prepared_ && hz >= nyquist_) {
            fundamental_ = nyquist_ - 0.001f;
        } else {
            fundamental_ = hz;
        }
    }

    // =========================================================================
    // Per-Partial Control (FR-008 to FR-012)
    // =========================================================================

    /// @brief Set amplitude of a specific partial (FR-009).
    ///
    /// @param partialNumber Partial number [1, kMaxPartials] (1 = fundamental).
    ///        Out-of-range values are silently ignored (FR-012).
    /// @param amplitude Amplitude in [0, 1]. Values outside range are clamped.
    ///
    /// @note Real-time safe
    void setPartialAmplitude(size_t partialNumber, float amplitude) noexcept {
        // FR-012: Out-of-range silently ignored
        if (partialNumber < 1 || partialNumber > kMaxPartials) {
            return;
        }

        // Sanitize NaN/Inf
        if (detail::isNaN(amplitude) || detail::isInf(amplitude)) {
            return;
        }

        // Convert to 0-based index and clamp amplitude
        size_t index = partialNumber - 1;
        partialAmplitudes_[index] = std::clamp(amplitude, 0.0f, 1.0f);
    }

    /// @brief Set frequency ratio of a specific partial relative to fundamental (FR-010).
    ///
    /// @param partialNumber Partial number [1, kMaxPartials] (1 = fundamental).
    ///        Out-of-range values are silently ignored (FR-012).
    /// @param ratio Frequency ratio in range (0, 64.0]. Invalid values (<=0, NaN, Inf)
    ///        are clamped to 0.001. Default for partial N is N.
    ///
    /// @note Real-time safe
    void setPartialFrequencyRatio(size_t partialNumber, float ratio) noexcept {
        // FR-012: Out-of-range silently ignored
        if (partialNumber < 1 || partialNumber > kMaxPartials) {
            return;
        }

        // Sanitize invalid values
        if (detail::isNaN(ratio) || detail::isInf(ratio) || ratio <= 0.0f) {
            ratio = kMinFrequencyRatio;
        }

        // Clamp to maximum
        if (ratio > kMaxFrequencyRatio) {
            ratio = kMaxFrequencyRatio;
        }

        size_t index = partialNumber - 1;
        partialRatios_[index] = ratio;
    }

    /// @brief Set initial phase of a specific partial (FR-011).
    ///
    /// @param partialNumber Partial number [1, kMaxPartials] (1 = fundamental).
    ///        Out-of-range values are silently ignored (FR-012).
    /// @param phase Phase in [0, 1) where 1.0 = 2*pi radians. Wrapped to [0, 1).
    ///
    /// @note Phase takes effect at next reset() call, not applied mid-playback.
    /// @note Real-time safe
    void setPartialPhase(size_t partialNumber, float phase) noexcept {
        // FR-012: Out-of-range silently ignored
        if (partialNumber < 1 || partialNumber > kMaxPartials) {
            return;
        }

        // Sanitize NaN/Inf
        if (detail::isNaN(phase) || detail::isInf(phase)) {
            return;
        }

        // Wrap to [0, 1)
        while (phase >= 1.0f) phase -= 1.0f;
        while (phase < 0.0f) phase += 1.0f;

        size_t index = partialNumber - 1;
        partialInitialPhases_[index] = phase;
    }

    // =========================================================================
    // Macro Controls (FR-013 to FR-017)
    // =========================================================================

    /// @brief Set number of active partials (FR-013).
    ///
    /// @param count Number of partials [1, kMaxPartials]. Clamped to range.
    ///
    /// @note Real-time safe
    void setNumPartials(size_t count) noexcept {
        numPartials_ = std::clamp(count, size_t{1}, kMaxPartials);
    }

    /// @brief Apply spectral tilt (dB/octave rolloff) to partial amplitudes (FR-014).
    ///
    /// @param tiltDb Tilt in dB/octave [-24, +12]. Positive boosts highs.
    ///
    /// @note Modifies effective amplitudes; does not change stored values.
    /// @note Real-time safe
    void setSpectralTilt(float tiltDb) noexcept {
        // Sanitize NaN/Inf
        if (detail::isNaN(tiltDb) || detail::isInf(tiltDb)) {
            return;
        }

        spectralTilt_ = std::clamp(tiltDb, kMinSpectralTilt, kMaxSpectralTilt);
    }

    /// @brief Set inharmonicity coefficient for partial frequency stretching (FR-016).
    ///
    /// @param B Inharmonicity coefficient [0, 0.1]. 0 = harmonic, higher = bell-like.
    ///
    /// @note Applies formula: f_n = n * f1 * sqrt(1 + B * n^2) where n is 1-based
    /// @note Real-time safe
    void setInharmonicity(float B) noexcept {
        // Sanitize NaN/Inf
        if (detail::isNaN(B) || detail::isInf(B)) {
            return;
        }

        inharmonicity_ = std::clamp(B, 0.0f, kMaxInharmonicity);
    }

    // =========================================================================
    // Processing (FR-018, FR-018a, FR-019 to FR-023)
    // =========================================================================

    /// @brief Generate output samples using IFFT overlap-add synthesis (FR-018).
    ///
    /// @param output Destination buffer (must hold numSamples floats)
    /// @param numSamples Number of samples to generate
    ///
    /// @pre prepare() has been called, otherwise outputs zeros (FR-018a)
    ///
    /// @note Real-time safe: noexcept, no allocations (FR-024, FR-025)
    void processBlock(float* output, size_t numSamples) noexcept {
        if (output == nullptr || numSamples == 0) {
            return;
        }

        // FR-018a: Output zeros if not prepared
        if (!prepared_) {
            std::fill(output, output + numSamples, 0.0f);
            return;
        }

        // FR-007: Output silence if fundamental is too low
        if (fundamental_ < kMinFundamental) {
            std::fill(output, output + numSamples, 0.0f);
            return;
        }

        // Generate samples via overlap-add
        for (size_t i = 0; i < numSamples; ++i) {
            // Synthesize new frames as needed
            while (samplesInBuffer_ < hopSize_) {
                synthesizeFrame();
            }

            // Pull sample from output buffer and sanitize (FR-022)
            float sample = outputBuffer_[outputReadIndex_];
            output[i] = sanitizeOutput(sample);
            outputBuffer_[outputReadIndex_] = 0.0f;  // Clear for next overlap-add
            outputReadIndex_ = (outputReadIndex_ + 1) % outputBuffer_.size();
            --samplesInBuffer_;
        }
    }

    // =========================================================================
    // Query
    // =========================================================================

    /// @brief Get processing latency in samples (FR-004).
    ///
    /// @return FFT size (latency equals one full FFT frame), or 0 if not prepared
    [[nodiscard]] size_t latency() const noexcept {
        return prepared_ ? fftSize_ : 0;
    }

    /// @brief Check if processor is prepared.
    ///
    /// @return true if prepare() has been called successfully
    [[nodiscard]] bool isPrepared() const noexcept {
        return prepared_;
    }

    /// @brief Get current sample rate.
    ///
    /// @return Sample rate in Hz, or 0 if not prepared
    [[nodiscard]] double sampleRate() const noexcept {
        return prepared_ ? sampleRate_ : 0.0;
    }

    /// @brief Get current FFT size.
    ///
    /// @return FFT size, or 0 if not prepared
    [[nodiscard]] size_t fftSize() const noexcept {
        return prepared_ ? fftSize_ : 0;
    }

    /// @brief Get current fundamental frequency.
    ///
    /// @return Fundamental in Hz
    [[nodiscard]] float fundamental() const noexcept {
        return fundamental_;
    }

    /// @brief Get number of active partials.
    ///
    /// @return Partial count [1, kMaxPartials]
    [[nodiscard]] size_t numPartials() const noexcept {
        return numPartials_;
    }

private:
    // =========================================================================
    // Private Methods
    // =========================================================================

    /// @brief Initialize default partial state
    void initializeDefaultState() noexcept {
        // Default values
        fundamental_ = 440.0f;
        numPartials_ = 1;
        spectralTilt_ = 0.0f;
        inharmonicity_ = 0.0f;

        // Initialize partial arrays
        partialAmplitudes_.fill(0.0f);
        partialAmplitudes_[0] = 1.0f;  // Fundamental at full amplitude

        // Default ratios: partial N has ratio N
        for (size_t i = 0; i < kMaxPartials; ++i) {
            partialRatios_[i] = static_cast<float>(i + 1);
        }

        partialInitialPhases_.fill(0.0f);
        accumulatedPhases_.fill(0.0);
    }

    /// @brief Calculate partial frequency with inharmonicity (FR-017)
    ///
    /// @param partialNumber 1-based partial number
    /// @return Frequency in Hz
    [[nodiscard]] float calculatePartialFrequency(size_t partialNumber) const noexcept {
        size_t index = partialNumber - 1;
        float ratio = partialRatios_[index];
        float n = static_cast<float>(partialNumber);

        // f_n = ratio * fundamental * sqrt(1 + B * n^2)
        float stretch = std::sqrt(1.0f + inharmonicity_ * n * n);
        return ratio * fundamental_ * stretch;
    }

    /// @brief Calculate tilt factor for a partial (FR-015)
    ///
    /// @param partialNumber 1-based partial number
    /// @return Amplitude multiplier
    [[nodiscard]] float calculateTiltFactor(size_t partialNumber) const noexcept {
        if (spectralTilt_ == 0.0f || partialNumber <= 1) {
            return 1.0f;
        }

        float n = static_cast<float>(partialNumber);
        // amplitude[n] = amplitude[n] * pow(10, tiltDb * log2(n) / 20)
        float log2n = std::log2(n);
        float dbChange = spectralTilt_ * log2n;
        return std::pow(10.0f, dbChange / 20.0f);
    }

    /// @brief Construct spectrum from partials
    void constructSpectrum() noexcept {
        // Clear spectrum
        for (size_t i = 0; i < numBins_; ++i) {
            spectrum_[i] = {0.0f, 0.0f};
        }

        // Amplitude scaling factor:
        // - FFT inverse already applies 1/N normalization
        // - Hann window at 75% overlap has COLA gain of approximately 1.5
        // - We need to scale up by N/2 for correct sinusoid amplitude from IFFT
        // - Empirical adjustment factor for peak ~1.0 with single partial at amp 1.0
        // Combined factor: (N/2) / 1.5 * adjustment = N/3.5
        float ampScale = static_cast<float>(fftSize_) / 3.5f;

        // Add each active partial to spectrum (FR-019)
        for (size_t p = 1; p <= numPartials_; ++p) {
            size_t index = p - 1;
            float amplitude = partialAmplitudes_[index];

            // Skip zero-amplitude partials
            if (amplitude < 1e-10f) {
                continue;
            }

            // Calculate frequency with inharmonicity
            float freq = calculatePartialFrequency(p);

            // FR-021: Skip partials above Nyquist
            if (freq >= nyquist_) {
                continue;
            }

            // Apply spectral tilt
            float tiltFactor = calculateTiltFactor(p);
            float effectiveAmp = amplitude * tiltFactor * ampScale;

            // Calculate FFT bin (FR-019: partials mapping to same bin are summed)
            float binFloat = freq * static_cast<float>(fftSize_) / static_cast<float>(sampleRate_);
            auto bin = static_cast<size_t>(std::round(binFloat));

            if (bin >= numBins_) {
                continue;
            }

            // Get phase for this partial (FR-023: phase continuity)
            double phase = accumulatedPhases_[index];

            // Convert phase to complex (phase is in [0, 1), convert to radians)
            // For IFFT: to get cos(2*pi*f*t + phi), we need:
            // X[k] = A * e^(j*phi) = A * (cos(phi) + j*sin(phi))
            float phaseRad = static_cast<float>(phase) * kTwoPi;
            float real = effectiveAmp * std::cos(phaseRad);
            float imag = effectiveAmp * std::sin(phaseRad);

            // Add to spectrum (FR-019: sum partials in same bin)
            spectrum_[bin].real += real;
            spectrum_[bin].imag += imag;
        }
    }

    /// @brief Advance phase accumulators for next frame
    void advancePhases() noexcept {
        for (size_t p = 1; p <= numPartials_; ++p) {
            size_t index = p - 1;

            // Skip zero-amplitude partials
            if (partialAmplitudes_[index] < 1e-10f) {
                continue;
            }

            float freq = calculatePartialFrequency(p);

            // Phase increment per hop: freq * hopSize / sampleRate
            double phaseInc = static_cast<double>(freq) *
                             static_cast<double>(hopSize_) / sampleRate_;

            accumulatedPhases_[index] += phaseInc;

            // Wrap to [0, 1)
            while (accumulatedPhases_[index] >= 1.0) {
                accumulatedPhases_[index] -= 1.0;
            }
        }
    }

    /// @brief Synthesize one IFFT frame with overlap-add
    void synthesizeFrame() noexcept {
        // Construct spectrum from partials
        constructSpectrum();

        // Inverse FFT
        fft_.inverse(spectrum_.data(), ifftBuffer_.data());

        // Apply Hann window and overlap-add (FR-019, FR-020)
        for (size_t i = 0; i < fftSize_; ++i) {
            float windowed = ifftBuffer_[i] * window_[i];

            // Overlap-add to output buffer
            size_t outIdx = (outputWriteIndex_ + i) % outputBuffer_.size();
            outputBuffer_[outIdx] += windowed;
        }

        // Advance write index by hop size
        outputWriteIndex_ = (outputWriteIndex_ + hopSize_) % outputBuffer_.size();
        samplesInBuffer_ += hopSize_;
        ++framesGenerated_;

        // Advance phase accumulators for next frame (FR-023)
        advancePhases();
    }

    /// @brief Sanitize output value (FR-022)
    [[nodiscard]] static float sanitizeOutput(float x) noexcept {
        // Check for NaN/Inf
        if (detail::isNaN(x) || detail::isInf(x)) {
            return 0.0f;
        }

        // Clamp to [-2, +2]
        return std::clamp(x, -2.0f, 2.0f);
    }

    // =========================================================================
    // Member Variables
    // =========================================================================

    // Configuration (set at prepare())
    double sampleRate_ = 0.0;     ///< Sample rate in Hz
    size_t fftSize_ = 0;          ///< FFT size (512, 1024, 2048, 4096)
    size_t hopSize_ = 0;          ///< Frame advance = fftSize / 4
    size_t numBins_ = 0;          ///< Number of spectrum bins = fftSize / 2 + 1
    float nyquist_ = 0.0f;        ///< Nyquist frequency = sampleRate / 2

    // Parameters (modifiable at runtime)
    float fundamental_ = 440.0f;     ///< Base frequency in Hz
    size_t numPartials_ = 1;         ///< Number of active partials [1, kMaxPartials]
    float spectralTilt_ = 0.0f;      ///< Spectral tilt in dB/octave [-24, +12]
    float inharmonicity_ = 0.0f;     ///< Inharmonicity coefficient B [0, 0.1]

    // Per-partial state (arrays of kMaxPartials)
    std::array<float, kMaxPartials> partialAmplitudes_{};     ///< User-set amplitude per partial [0, 1]
    std::array<float, kMaxPartials> partialRatios_{};         ///< Frequency ratio per partial (default = partial number)
    std::array<float, kMaxPartials> partialInitialPhases_{};  ///< Initial phase (normalized), applied at reset()
    std::array<double, kMaxPartials> accumulatedPhases_{};    ///< Running phase accumulator per partial

    // Processing resources (allocated in prepare())
    FFT fft_;                              ///< FFT processor instance
    std::vector<Complex> spectrum_;        ///< Working spectrum buffer
    std::vector<float> ifftBuffer_;        ///< Time-domain IFFT output
    std::vector<float> window_;            ///< Hann window coefficients
    std::vector<float> outputBuffer_;      ///< Circular output accumulator

    // Runtime state
    size_t outputWriteIndex_ = 0;    ///< Write position in output buffer
    size_t outputReadIndex_ = 0;     ///< Read position in output buffer
    size_t samplesInBuffer_ = 0;     ///< Available output samples
    size_t framesGenerated_ = 0;     ///< Count of synthesized frames
    bool prepared_ = false;          ///< True after prepare() called
};

} // namespace DSP
} // namespace Krate
