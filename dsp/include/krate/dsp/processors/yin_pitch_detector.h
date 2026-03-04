// ==============================================================================
// Layer 2: DSP Processor - YIN Pitch Detector
// ==============================================================================
// YIN-based fundamental frequency (F0) tracker with FFT acceleration.
// Implements the de Cheveigne & Kawahara (2002) algorithm.
//
// Spec: specs/115-innexus-m1-core-instrument/spec.md
// Covers: FR-010 (CMNDF), FR-011 (FFT-accelerated difference function),
//         FR-012 (parabolic interpolation), FR-013 (F0Estimate output),
//         FR-014 (configurable F0 range), FR-015 (confidence gating),
//         FR-016 (frequency hysteresis), FR-017 (hold-previous F0)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, allocations only in prepare())
// - Principle III: Modern C++ (C++20, RAII)
// - Principle IX: Layer 2 (depends on Layer 0 core, Layer 1 primitives)
// - Principle XV: ODR Prevention (header-only, inline)
// ==============================================================================

#pragma once

#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/primitives/spectral_utils.h>

#include <pffft/pffft.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace Krate::DSP {

/// @brief YIN-based fundamental frequency (F0) pitch detector.
///
/// Uses the Cumulative Mean Normalized Difference Function (CMNDF) with
/// FFT-accelerated difference function computation (Wiener-Khinchin theorem).
/// Outputs F0Estimate with frequency, confidence, and voiced/unvoiced flag.
///
/// Features:
/// - FFT-accelerated O(N log N) per frame (FR-011)
/// - Parabolic interpolation for sub-sample precision (FR-012)
/// - Configurable F0 range (FR-014)
/// - Confidence gating (FR-015)
/// - Frequency hysteresis ~2% band (FR-016)
/// - Hold-previous F0 on confidence drop (FR-017)
class YinPitchDetector {
public:
    /// @brief Construct a YIN pitch detector.
    /// @param windowSize Analysis window size in samples (default 2048)
    /// @param minF0 Minimum detectable F0 in Hz (default 40 Hz) (FR-014)
    /// @param maxF0 Maximum detectable F0 in Hz (default 2000 Hz) (FR-014)
    /// @param confidenceThreshold Minimum confidence for voiced (default 0.3) (FR-015)
    explicit YinPitchDetector(size_t windowSize = 2048,
                              float minF0 = 40.0f,
                              float maxF0 = 2000.0f,
                              float confidenceThreshold = 0.3f) noexcept
        : windowSize_(windowSize),
          minF0_(minF0),
          maxF0_(maxF0),
          confidenceThreshold_(confidenceThreshold) {}

    ~YinPitchDetector() noexcept { destroy(); }

    // Non-copyable
    YinPitchDetector(const YinPitchDetector&) = delete;
    YinPitchDetector& operator=(const YinPitchDetector&) = delete;

    // Movable
    YinPitchDetector(YinPitchDetector&& other) noexcept
        : windowSize_(other.windowSize_),
          minF0_(other.minF0_),
          maxF0_(other.maxF0_),
          confidenceThreshold_(other.confidenceThreshold_),
          sampleRate_(other.sampleRate_),
          fftSize_(other.fftSize_),
          setup_(other.setup_),
          padded_(other.padded_),
          spectrum_(other.spectrum_),
          work_(other.work_),
          autocorr_(other.autocorr_),
          diffBuf_(std::move(other.diffBuf_)),
          cmndfBuf_(std::move(other.cmndfBuf_)),
          previousGoodF0_(other.previousGoodF0_),
          previousConfidence_(other.previousConfidence_),
          prepared_(other.prepared_) {
        other.setup_ = nullptr;
        other.padded_ = nullptr;
        other.spectrum_ = nullptr;
        other.work_ = nullptr;
        other.autocorr_ = nullptr;
        other.prepared_ = false;
    }

    YinPitchDetector& operator=(YinPitchDetector&& other) noexcept {
        if (this != &other) {
            destroy();
            windowSize_ = other.windowSize_;
            minF0_ = other.minF0_;
            maxF0_ = other.maxF0_;
            confidenceThreshold_ = other.confidenceThreshold_;
            sampleRate_ = other.sampleRate_;
            fftSize_ = other.fftSize_;
            setup_ = other.setup_;
            padded_ = other.padded_;
            spectrum_ = other.spectrum_;
            work_ = other.work_;
            autocorr_ = other.autocorr_;
            diffBuf_ = std::move(other.diffBuf_);
            cmndfBuf_ = std::move(other.cmndfBuf_);
            previousGoodF0_ = other.previousGoodF0_;
            previousConfidence_ = other.previousConfidence_;
            prepared_ = other.prepared_;
            other.setup_ = nullptr;
            other.padded_ = nullptr;
            other.spectrum_ = nullptr;
            other.work_ = nullptr;
            other.autocorr_ = nullptr;
            other.prepared_ = false;
        }
        return *this;
    }

    /// @brief Prepare the detector for a given sample rate.
    /// Allocates FFT buffers. NOT real-time safe.
    /// @param sampleRate Sample rate in Hz
    void prepare(double sampleRate) noexcept {
        destroy();

        sampleRate_ = sampleRate;

        // FFT size = next power of 2 >= 2 * windowSize (for linear correlation)
        size_t minSize = windowSize_ * 2;
        fftSize_ = 1;
        while (fftSize_ < minSize) {
            fftSize_ *= 2;
        }

        // pffft minimum for real transforms is 32
        if (fftSize_ < 32) fftSize_ = 32;

        setup_ = pffft_new_setup(static_cast<int>(fftSize_), PFFFT_REAL);
        if (!setup_) {
            fftSize_ = 0;
            return;
        }

        // Allocate SIMD-aligned buffers for FFT
        padded_ = static_cast<float*>(pffft_aligned_malloc(fftSize_ * sizeof(float)));
        spectrum_ = static_cast<float*>(pffft_aligned_malloc(fftSize_ * sizeof(float)));
        work_ = static_cast<float*>(pffft_aligned_malloc(fftSize_ * sizeof(float)));
        autocorr_ = static_cast<float*>(pffft_aligned_malloc(fftSize_ * sizeof(float)));

        if (!padded_ || !spectrum_ || !work_ || !autocorr_) {
            destroy();
            return;
        }

        // Zero all buffers
        std::fill_n(padded_, fftSize_, 0.0f);
        std::fill_n(spectrum_, fftSize_, 0.0f);
        std::fill_n(work_, fftSize_, 0.0f);
        std::fill_n(autocorr_, fftSize_, 0.0f);

        // Allocate difference function and CMNDF buffers
        // Maximum lag we ever search is windowSize_ / 2
        const size_t maxLag = windowSize_ / 2 + 1;
        diffBuf_.resize(maxLag, 0.0f);
        cmndfBuf_.resize(maxLag, 0.0f);

        prepared_ = true;
    }

    /// @brief Reset internal state (previous F0, confidence).
    /// Real-time safe.
    void reset() noexcept {
        previousGoodF0_ = 0.0f;
        previousConfidence_ = 0.0f;
    }

    /// @brief Detect the fundamental frequency in a buffer of audio samples.
    /// @param samples Input audio buffer
    /// @param numSamples Number of samples (should be >= windowSize)
    /// @return F0Estimate with frequency, confidence, and voiced flag (FR-013)
    /// @note Uses FFT-accelerated difference function (FR-011)
    [[nodiscard]] F0Estimate detect(const float* samples,
                                     size_t numSamples) noexcept {
        if (!prepared_ || samples == nullptr || numSamples == 0) {
            return makeUnvoicedEstimate();
        }

        // Use at most windowSize_ samples
        const size_t N = std::min(numSamples, windowSize_);
        // Maximum lag for the difference function: N/2
        const size_t W = N / 2;

        if (W < 2) {
            return makeUnvoicedEstimate();
        }

        // Step 1: Compute autocorrelation via FFT (FR-011)
        computeAutocorrelation(samples, N);

        // Step 2: Compute difference function from autocorrelation
        computeDifferenceFunction(samples, N, W);

        // Step 3: Compute CMNDF (FR-010)
        computeCMNDF(W);

        // Step 4: Find the best tau via absolute threshold search
        const size_t minTau = computeMinTau();
        const size_t maxTau = computeMaxTau(W);

        if (minTau >= maxTau || minTau >= W) {
            return makeUnvoicedEstimate();
        }

        // Find first tau where CMNDF < threshold (absolute threshold search)
        size_t bestTau = 0;
        float bestCmndf = 1.0f;
        bool found = false;

        for (size_t tau = minTau; tau < maxTau; ++tau) {
            if (cmndfBuf_[tau] < confidenceThreshold_) {
                // Found a tau below threshold; now find the local minimum
                // in this valley
                size_t valleyTau = tau;
                while (valleyTau + 1 < maxTau &&
                       cmndfBuf_[valleyTau + 1] < cmndfBuf_[valleyTau]) {
                    ++valleyTau;
                }
                bestTau = valleyTau;
                bestCmndf = cmndfBuf_[valleyTau];
                found = true;
                break;
            }
        }

        // If no tau below threshold found, search for the global minimum
        if (!found) {
            bestCmndf = 1.0f;
            for (size_t tau = minTau; tau < maxTau; ++tau) {
                if (cmndfBuf_[tau] < bestCmndf) {
                    bestCmndf = cmndfBuf_[tau];
                    bestTau = tau;
                }
            }
        }

        if (bestTau == 0) {
            return makeUnvoicedEstimate();
        }

        // Step 5: Parabolic interpolation for sub-sample precision (FR-012)
        float interpolatedTau = static_cast<float>(bestTau);
        if (bestTau > 0 && bestTau + 1 < W) {
            interpolatedTau = parabolicInterpolation(
                cmndfBuf_[bestTau - 1],
                cmndfBuf_[bestTau],
                cmndfBuf_[bestTau + 1],
                static_cast<float>(bestTau));
        }

        // Step 6: Convert tau to frequency
        float frequency = static_cast<float>(sampleRate_) / interpolatedTau;
        float confidence = 1.0f - bestCmndf;
        confidence = std::clamp(confidence, 0.0f, 1.0f);

        // Validate frequency is within range
        if (frequency < minF0_ || frequency > maxF0_) {
            return applyStabilityFeatures(0.0f, 0.0f);
        }

        // Step 7: Apply stability features (FR-015, FR-016, FR-017)
        return applyStabilityFeatures(frequency, confidence);
    }

private:
    // -------------------------------------------------------------------------
    // Configuration
    // -------------------------------------------------------------------------
    size_t windowSize_ = 2048;
    float minF0_ = 40.0f;
    float maxF0_ = 2000.0f;
    float confidenceThreshold_ = 0.3f;

    // -------------------------------------------------------------------------
    // Runtime state
    // -------------------------------------------------------------------------
    double sampleRate_ = 44100.0;
    size_t fftSize_ = 0;

    // pffft resources (raw pointers, managed manually like FFTAutocorrelation)
    PFFFT_Setup* setup_ = nullptr;
    float* padded_ = nullptr;
    float* spectrum_ = nullptr;
    float* work_ = nullptr;
    float* autocorr_ = nullptr;

    // Difference function and CMNDF buffers
    std::vector<float> diffBuf_;
    std::vector<float> cmndfBuf_;

    // Stability state (FR-016, FR-017)
    float previousGoodF0_ = 0.0f;
    float previousConfidence_ = 0.0f;

    bool prepared_ = false;

    // Hysteresis band fraction (~2%) (FR-016)
    static constexpr float kHysteresisBand = 0.02f;

    // -------------------------------------------------------------------------
    // FFT-Accelerated Autocorrelation (FR-011)
    // -------------------------------------------------------------------------

    /// Compute the autocorrelation of the signal using FFT (Wiener-Khinchin theorem).
    /// The signal is zero-padded to fftSize_ (>= 2*N) to get linear (not circular)
    /// autocorrelation. Result is stored in autocorr_ buffer (unnormalized).
    ///
    /// autocorr_[tau] = fftSize_ * sum_{j=0}^{N-1-tau} x[j]*x[j+tau]
    void computeAutocorrelation(const float* signal, size_t N) noexcept {
        // Zero-pad signal into aligned buffer
        std::copy_n(signal, N, padded_);
        std::fill(padded_ + N, padded_ + fftSize_, 0.0f);

        // Forward FFT
        pffft_transform_ordered(setup_, padded_, spectrum_, work_, PFFFT_FORWARD);

        // Compute power spectrum |X(k)|^2 in-place
        // pffft ordered format: [DC, Nyquist, Re(1), Im(1), Re(2), Im(2), ...]
        spectrum_[0] = spectrum_[0] * spectrum_[0]; // DC (real only)
        spectrum_[1] = spectrum_[1] * spectrum_[1]; // Nyquist (real only)
        for (size_t k = 1; k < fftSize_ / 2; ++k) {
            const float re = spectrum_[2 * k];
            const float im = spectrum_[2 * k + 1];
            spectrum_[2 * k] = re * re + im * im;
            spectrum_[2 * k + 1] = 0.0f;
        }

        // Inverse FFT -> raw (unnormalized) autocorrelation
        pffft_transform_ordered(setup_, spectrum_, autocorr_, work_, PFFFT_BACKWARD);
    }

    // -------------------------------------------------------------------------
    // Difference Function (using autocorrelation)
    // -------------------------------------------------------------------------

    /// Compute the type-II difference function d(tau) from the autocorrelation.
    ///
    /// The YIN difference function for lag tau is:
    ///   d(tau) = sum_{j=0}^{W-1} (x[j] - x[j+tau])^2
    ///
    /// This expands to:
    ///   d(tau) = sum_{j=0}^{W-1} x[j]^2 + sum_{j=0}^{W-1} x[j+tau]^2
    ///            - 2 * sum_{j=0}^{W-1} x[j]*x[j+tau]
    ///
    /// The cross-correlation term is obtained from the FFT autocorrelation.
    /// Since we zero-padded to >= 2*N, and W <= N/2, the autocorrelation
    /// at lag tau naturally gives us terms for j = 0..N-1-tau, which includes
    /// the range j = 0..W-1 (since tau < W and W + tau <= N).
    ///
    /// However, the FFT autocorrelation sums beyond j=W-1 for small tau.
    /// To get the exact windowed cross-correlation, we compute it directly
    /// using the squared running sums approach (O(N) per frame).
    void computeDifferenceFunction(const float* signal, size_t N,
                                   size_t W) noexcept {
        const float fftScale = 1.0f / static_cast<float>(fftSize_);

        // Prefix energy sum: prefixE[i] = sum(x[j]^2, j=0..i-1)
        // We reuse padded_ buffer (no longer needed after autocorrelation)
        float* prefixE = padded_;
        prefixE[0] = 0.0f;
        for (size_t i = 0; i < N; ++i) {
            prefixE[i + 1] = prefixE[i] + signal[i] * signal[i];
        }

        diffBuf_[0] = 0.0f; // d(0) = 0 by definition

        for (size_t tau = 1; tau < W; ++tau) {
            // Note: Windowed energies e1 = prefixE[W] and
            // e2 = prefixE[tau+W] - prefixE[tau] are not used here because
            // the full-range difference function (below) is preferred for
            // robustness. The CMNDF normalizes it appropriately.

            // Cross-correlation from FFT autocorrelation:
            // R_fft(tau) = sum(x[j]*x[j+tau], j=0..N-1-tau)
            //
            // We need: R_W(tau) = sum(x[j]*x[j+tau], j=0..W-1)
            //
            // Since N >= 2*W and tau < W, the FFT autocorrelation includes
            // extra terms for j = W .. N-1-tau. We correct by subtracting them.
            // But computing those extra terms would require another O(N) pass.
            //
            // Instead, use a Type-II difference function approach:
            // Compute d(tau) incrementally using the autocorrelation.
            //
            // For the standard YIN FFT approach (Bittner et al.):
            // d(tau) = autocorr(0) + [autocorr(0) shifted by tau] - 2*autocorr(tau)
            //
            // where autocorr(0) = total energy, shifted means from position tau.
            //
            // This gives d(tau) = sum_{j=0}^{N-1-tau} (x[j]-x[j+tau])^2
            // which sums over MORE terms than our W-windowed version.
            //
            // For YIN to work correctly, this full-length difference function
            // is actually preferred (more robust). The CMNDF normalizes it.
            //
            // Full-range d(tau):
            const float eFull0 = prefixE[N - tau]; // sum(x[j]^2, j=0..N-1-tau)
            const float eFullT = prefixE[N] - prefixE[tau]; // sum(x[j]^2, j=tau..N-1)
            const float crossCorr = autocorr_[tau] * fftScale;

            // Use the full-range version (matches FFT autocorrelation exactly)
            diffBuf_[tau] = eFull0 + eFullT - 2.0f * crossCorr;

            // Clamp to non-negative (numerical errors can produce small negatives)
            if (diffBuf_[tau] < 0.0f) {
                diffBuf_[tau] = 0.0f;
            }
        }
    }

    // -------------------------------------------------------------------------
    // CMNDF (FR-010)
    // -------------------------------------------------------------------------

    /// Compute the Cumulative Mean Normalized Difference Function.
    /// d'(tau) = 1                              if tau == 0
    /// d'(tau) = d(tau) / ((1/tau) * sum(d(j), j=1..tau))  otherwise
    void computeCMNDF(size_t W) noexcept {
        cmndfBuf_[0] = 1.0f;

        float cumulativeSum = 0.0f;
        for (size_t tau = 1; tau < W; ++tau) {
            cumulativeSum += diffBuf_[tau];
            if (cumulativeSum < 1e-10f) {
                cmndfBuf_[tau] = 1.0f;
            } else {
                cmndfBuf_[tau] = diffBuf_[tau] * static_cast<float>(tau) /
                                 cumulativeSum;
            }
        }
    }

    // -------------------------------------------------------------------------
    // Lag Range Computation (FR-014)
    // -------------------------------------------------------------------------

    /// Compute minimum tau from maxF0 (higher frequency = shorter period)
    [[nodiscard]] size_t computeMinTau() const noexcept {
        if (maxF0_ <= 0.0f) return 1;
        auto tau = static_cast<size_t>(
            std::floor(static_cast<float>(sampleRate_) / maxF0_));
        return std::max(tau, static_cast<size_t>(1));
    }

    /// Compute maximum tau from minF0 (lower frequency = longer period)
    [[nodiscard]] size_t computeMaxTau(size_t W) const noexcept {
        if (minF0_ <= 0.0f) return W;
        auto tau = static_cast<size_t>(
            std::ceil(static_cast<float>(sampleRate_) / minF0_));
        return std::min(tau + 1, W); // +1 because the loop uses tau < maxTau
    }

    // -------------------------------------------------------------------------
    // Stability Features (FR-015, FR-016, FR-017)
    // -------------------------------------------------------------------------

    /// Apply confidence gating, frequency hysteresis, and hold-previous logic.
    [[nodiscard]] F0Estimate applyStabilityFeatures(float rawFrequency,
                                                     float confidence) noexcept {
        F0Estimate result;

        // FR-015: Confidence gating
        bool isVoiced = confidence >= confidenceThreshold_ && rawFrequency > 0.0f;

        if (isVoiced) {
            float finalFreq = rawFrequency;

            // FR-016: Frequency hysteresis (~2% band)
            if (previousGoodF0_ > 0.0f) {
                float ratio = std::abs(rawFrequency - previousGoodF0_) /
                              previousGoodF0_;
                if (ratio < kHysteresisBand) {
                    finalFreq = previousGoodF0_;
                }
            }

            result.frequency = finalFreq;
            result.confidence = confidence;
            result.voiced = true;

            // Update state
            previousGoodF0_ = finalFreq;
            previousConfidence_ = confidence;
        } else {
            // FR-017: Hold previous F0 when confidence drops
            result.frequency = previousGoodF0_;
            result.confidence = confidence;
            result.voiced = false;

            // Do NOT update previousGoodF0_ -- keep holding
        }

        return result;
    }

    /// Create an unvoiced estimate (uses hold-previous logic)
    [[nodiscard]] F0Estimate makeUnvoicedEstimate() noexcept {
        return applyStabilityFeatures(0.0f, 0.0f);
    }

    // -------------------------------------------------------------------------
    // Resource Management
    // -------------------------------------------------------------------------

    void destroy() noexcept {
        if (setup_) {
            pffft_destroy_setup(setup_);
            setup_ = nullptr;
        }
        if (padded_) {
            pffft_aligned_free(padded_);
            padded_ = nullptr;
        }
        if (spectrum_) {
            pffft_aligned_free(spectrum_);
            spectrum_ = nullptr;
        }
        if (work_) {
            pffft_aligned_free(work_);
            work_ = nullptr;
        }
        if (autocorr_) {
            pffft_aligned_free(autocorr_);
            autocorr_ = nullptr;
        }
        diffBuf_.clear();
        cmndfBuf_.clear();
        fftSize_ = 0;
        prepared_ = false;
    }
};

} // namespace Krate::DSP
