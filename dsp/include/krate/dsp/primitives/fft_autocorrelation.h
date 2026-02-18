// ==============================================================================
// Layer 1: DSP Primitive - FFT-Based Autocorrelation
// ==============================================================================
// Computes autocorrelation in O(N log N) using FFT instead of O(N * maxLag).
// Uses pffft directly for SIMD-accelerated computation.
//
// Algorithm: autocorrelation(x) = IFFT(|FFT(zero_pad(x))|^2)
// Zero-padding to 2N avoids circular correlation artifacts.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, allocations only in prepare())
// - Principle III: Modern C++ (C++20, RAII)
// - Principle IX: Layer 1 (depends only on Layer 0 / pffft)
// ==============================================================================

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>

#include <pffft.h>

namespace Krate::DSP {

/// @brief FFT-based autocorrelation for O(N log N) computation.
///
/// Replaces the naive O(N * maxLag) nested loop with:
///   1. Zero-pad signal to 2N
///   2. Forward FFT (SIMD-accelerated via pffft)
///   3. Power spectrum: |X(k)|^2
///   4. Inverse FFT
///   5. Normalize
///
/// Designed for real-time pitch detection where the autocorrelation
/// of an analysis window needs to be computed repeatedly.
class FFTAutocorrelation {
public:
    FFTAutocorrelation() noexcept = default;

    ~FFTAutocorrelation() noexcept {
        destroy();
    }

    // Non-copyable
    FFTAutocorrelation(const FFTAutocorrelation&) = delete;
    FFTAutocorrelation& operator=(const FFTAutocorrelation&) = delete;

    // Movable
    FFTAutocorrelation(FFTAutocorrelation&& other) noexcept
        : fftSize_(other.fftSize_)
        , setup_(other.setup_)
        , padded_(other.padded_)
        , spectrum_(other.spectrum_)
        , work_(other.work_)
        , result_(other.result_) {
        other.setup_ = nullptr;
        other.padded_ = nullptr;
        other.spectrum_ = nullptr;
        other.work_ = nullptr;
        other.result_ = nullptr;
        other.fftSize_ = 0;
    }

    FFTAutocorrelation& operator=(FFTAutocorrelation&& other) noexcept {
        if (this != &other) {
            destroy();
            fftSize_ = other.fftSize_;
            setup_ = other.setup_;
            padded_ = other.padded_;
            spectrum_ = other.spectrum_;
            work_ = other.work_;
            result_ = other.result_;
            other.setup_ = nullptr;
            other.padded_ = nullptr;
            other.spectrum_ = nullptr;
            other.work_ = nullptr;
            other.result_ = nullptr;
            other.fftSize_ = 0;
        }
        return *this;
    }

    /// @brief Prepare for a given window size.
    ///
    /// Allocates the FFT setup and SIMD-aligned buffers for 2*windowSize FFT.
    /// The FFT size is rounded up to the next power of 2 >= 2*windowSize.
    ///
    /// @param windowSize  Analysis window size in samples (e.g. 256)
    /// @note NOT real-time safe (allocates)
    void prepare(std::size_t windowSize) noexcept {
        destroy();

        // FFT size = next power of 2 >= 2 * windowSize (zero-padding for linear correlation)
        std::size_t minSize = windowSize * 2;
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

        // Allocate SIMD-aligned buffers
        padded_ = static_cast<float*>(pffft_aligned_malloc(fftSize_ * sizeof(float)));
        spectrum_ = static_cast<float*>(pffft_aligned_malloc(fftSize_ * sizeof(float)));
        work_ = static_cast<float*>(pffft_aligned_malloc(fftSize_ * sizeof(float)));
        result_ = static_cast<float*>(pffft_aligned_malloc(fftSize_ * sizeof(float)));

        if (!padded_ || !spectrum_ || !work_ || !result_) {
            destroy();
            return;
        }

        // Zero all buffers
        std::fill_n(padded_, fftSize_, 0.0f);
        std::fill_n(spectrum_, fftSize_, 0.0f);
        std::fill_n(work_, fftSize_, 0.0f);
        std::fill_n(result_, fftSize_, 0.0f);
    }

    /// @brief Check if prepare() succeeded.
    [[nodiscard]] bool isPrepared() const noexcept {
        return setup_ != nullptr && fftSize_ > 0;
    }

    /// @brief Compute normalized autocorrelation of a signal.
    ///
    /// Uses FFT for the cross-correlation, then applies proper per-lag
    /// energy normalization matching the behavior of naive O(N^2) method:
    ///   autocorr[lag] = sum(x[i]*x[i+lag]) / sqrt(E_head * E_tail)
    /// where E_head = sum(x[i]^2, i=0..N-lag-1), E_tail = sum(x[i]^2, i=lag..N-1)
    ///
    /// The per-lag energies are computed in O(N) via prefix sums, making the
    /// total cost O(N log N) for the FFT + O(N) for normalization.
    ///
    /// @param signal     Input signal (windowSize samples, need not be aligned)
    /// @param windowSize Number of valid samples in signal
    /// @param autocorr   Output autocorrelation values for lags [0..maxLag]
    /// @param minLag     Minimum lag to compute (inclusive)
    /// @param maxLag     Maximum lag to compute (inclusive, must be < windowSize)
    ///
    /// @note Real-time safe, noexcept. All buffers pre-allocated in prepare().
    void compute(const float* signal, std::size_t windowSize,
                 float* autocorr, std::size_t minLag,
                 std::size_t maxLag) noexcept {
        if (!isPrepared() || signal == nullptr || autocorr == nullptr) return;
        if (maxLag >= windowSize || maxLag >= fftSize_ / 2) return;

        // Step 1: Zero-pad signal into aligned buffer
        std::copy_n(signal, windowSize, padded_);
        std::fill(padded_ + windowSize, padded_ + fftSize_, 0.0f);

        // Step 2: Forward FFT (SIMD-accelerated)
        pffft_transform_ordered(setup_, padded_, spectrum_, work_, PFFFT_FORWARD);

        // Step 3: Power spectrum |X(k)|^2
        // pffft ordered format for real: [DC, Nyquist, Re(1), Im(1), Re(2), Im(2), ...]
        spectrum_[0] = spectrum_[0] * spectrum_[0];         // DC^2
        spectrum_[1] = spectrum_[1] * spectrum_[1];         // Nyquist^2
        for (std::size_t k = 1; k < fftSize_ / 2; ++k) {
            const float re = spectrum_[2 * k];
            const float im = spectrum_[2 * k + 1];
            spectrum_[2 * k] = re * re + im * im;
            spectrum_[2 * k + 1] = 0.0f;
        }

        // Step 4: Inverse FFT -> raw (unnormalized) autocorrelation
        pffft_transform_ordered(setup_, spectrum_, result_, work_, PFFFT_BACKWARD);

        // Step 5: Per-lag energy normalization matching the naive O(N^2) method.
        //
        // The naive method normalizes each lag as:
        //   autocorr[lag] = sum(x[i]*x[i+lag]) / sqrt(E_full * E_tail)
        // where:
        //   E_full = sum(x[i]^2) for ALL i = 0..N-1  (full window energy)
        //   E_tail = sum(x[i+lag]^2) for i = 0..N-lag-1 = sum(x[i]^2, i=lag..N-1)
        //
        // We precompute E_tail for each lag via prefix sums in O(N).
        const float fftScale = 1.0f / static_cast<float>(fftSize_);

        // Build prefix energy sum in the padded_ buffer (reuse, no longer needed)
        float* prefixE = padded_;
        prefixE[0] = 0.0f;
        for (std::size_t i = 0; i < windowSize; ++i) {
            prefixE[i + 1] = prefixE[i] + signal[i] * signal[i];
        }
        const float totalE = prefixE[windowSize];  // E_full

        if (totalE < 1e-10f) {
            for (std::size_t lag = minLag; lag <= maxLag; ++lag) {
                autocorr[lag] = 0.0f;
            }
            return;
        }

        for (std::size_t lag = minLag; lag <= maxLag; ++lag) {
            // E_tail(lag) = energy of x[lag..N-1]
            const float eTail = totalE - prefixE[lag];
            const float denom = std::sqrt(totalE * eTail);

            if (denom < 1e-10f) {
                autocorr[lag] = 0.0f;
            } else {
                autocorr[lag] = result_[lag] * fftScale / denom;
            }
        }
    }

private:
    void destroy() noexcept {
        if (setup_) { pffft_destroy_setup(setup_); setup_ = nullptr; }
        if (padded_) { pffft_aligned_free(padded_); padded_ = nullptr; }
        if (spectrum_) { pffft_aligned_free(spectrum_); spectrum_ = nullptr; }
        if (work_) { pffft_aligned_free(work_); work_ = nullptr; }
        if (result_) { pffft_aligned_free(result_); result_ = nullptr; }
        fftSize_ = 0;
    }

    std::size_t fftSize_ = 0;
    PFFFT_Setup* setup_ = nullptr;
    float* padded_ = nullptr;     // Zero-padded input (fftSize)
    float* spectrum_ = nullptr;   // FFT output / power spectrum (fftSize)
    float* work_ = nullptr;       // pffft work buffer (fftSize)
    float* result_ = nullptr;     // IFFT result = autocorrelation (fftSize)
};

} // namespace Krate::DSP
