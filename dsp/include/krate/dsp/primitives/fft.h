// ==============================================================================
// Layer 1: DSP Primitive - Fast Fourier Transform
// ==============================================================================
// SIMD-accelerated FFT via pffft (Pretty Fast FFT).
// Provides forward (real-to-complex) and inverse (complex-to-real) transforms.
// Uses SSE on x86/x64, NEON on ARM, with scalar fallback.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, allocations only in prepare())
// - Principle III: Modern C++ (C++20, RAII, constexpr)
// - Principle IX: Layer 1 (depends only on Layer 0)
// - Principle X: DSP Constraints (O(N log N) complexity)
// - Principle XII: Test-First Development
//
// Reference: specs/007-fft-processor/spec.md
// Backend: pffft (marton78 fork, BSD license)
// ==============================================================================

#pragma once

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <memory>

#include <pffft.h>

namespace Krate {
namespace DSP {

// =============================================================================
// Forward Declarations
// =============================================================================

struct Complex;
class FFT;

// =============================================================================
// Constants
// =============================================================================

/// Minimum supported FFT size
inline constexpr size_t kMinFFTSize = 256;

/// Maximum supported FFT size
inline constexpr size_t kMaxFFTSize = 8192;

// =============================================================================
// Complex Number (POD)
// =============================================================================

/// @brief Simple complex number for FFT operations
/// @note POD type for performance - no virtual functions
struct Complex {
    float real = 0.0f;  ///< Real component
    float imag = 0.0f;  ///< Imaginary component

    // -------------------------------------------------------------------------
    // Arithmetic Operators
    // -------------------------------------------------------------------------

    [[nodiscard]] constexpr Complex operator+(const Complex& other) const noexcept {
        return {real + other.real, imag + other.imag};
    }

    [[nodiscard]] constexpr Complex operator-(const Complex& other) const noexcept {
        return {real - other.real, imag - other.imag};
    }

    [[nodiscard]] constexpr Complex operator*(const Complex& other) const noexcept {
        return {
            real * other.real - imag * other.imag,
            real * other.imag + imag * other.real
        };
    }

    [[nodiscard]] constexpr Complex conjugate() const noexcept {
        return {real, -imag};
    }

    // -------------------------------------------------------------------------
    // Polar Representation
    // -------------------------------------------------------------------------

    /// @brief Get magnitude |z| = sqrt(real^2 + imag^2)
    [[nodiscard]] float magnitude() const noexcept {
        return std::sqrt(real * real + imag * imag);
    }

    /// @brief Get phase angle in radians
    [[nodiscard]] float phase() const noexcept {
        return std::atan2(imag, real);
    }
};

// =============================================================================
// RAII Helpers for pffft Resources
// =============================================================================

namespace detail {

struct PffftSetupDeleter {
    void operator()(PFFFT_Setup* s) const noexcept {
        if (s) pffft_destroy_setup(s);
    }
};

struct PffftAlignedDeleter {
    void operator()(void* p) const noexcept {
        if (p) pffft_aligned_free(p);
    }
};

/// Allocate a SIMD-aligned float buffer via pffft
inline std::unique_ptr<float, PffftAlignedDeleter> makeAlignedBuffer(size_t numFloats) {
    return {static_cast<float*>(pffft_aligned_malloc(numFloats * sizeof(float))),
            PffftAlignedDeleter{}};
}

} // namespace detail

// =============================================================================
// FFT Class
// =============================================================================

/// @brief Core Fast Fourier Transform processor (SIMD-accelerated via pffft)
class FFT {
public:
    FFT() noexcept = default;
    ~FFT() noexcept = default;

    // Non-copyable, movable (unique_ptr members enable default move)
    FFT(const FFT&) = delete;
    FFT& operator=(const FFT&) = delete;
    FFT(FFT&&) noexcept = default;
    FFT& operator=(FFT&&) noexcept = default;

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    /// @brief Prepare FFT for given size (allocates pffft setup and aligned buffers)
    /// @param fftSize Power of 2 in range [256, 8192]
    /// @pre fftSize is power of 2
    /// @note NOT real-time safe (allocates memory)
    void prepare(size_t fftSize) noexcept {
        size_ = fftSize;

        // Validate power of 2
        if (fftSize == 0 || !std::has_single_bit(fftSize)) {
            size_ = 0;
            return;
        }

        // Create pffft setup for real-valued transforms
        setup_.reset(pffft_new_setup(static_cast<int>(fftSize), PFFFT_REAL));
        if (!setup_) {
            size_ = 0;
            return;
        }

        // Allocate SIMD-aligned buffers (16-byte on SSE, as required by pffft)
        buf1_ = detail::makeAlignedBuffer(fftSize);
        buf2_ = detail::makeAlignedBuffer(fftSize);
        work_ = detail::makeAlignedBuffer(fftSize);
    }

    /// @brief Reset internal work buffers
    /// @note Real-time safe
    void reset() noexcept {
        if (buf1_) std::fill_n(buf1_.get(), size_, 0.0f);
        if (buf2_) std::fill_n(buf2_.get(), size_, 0.0f);
        if (work_) std::fill_n(work_.get(), size_, 0.0f);
    }

    // -------------------------------------------------------------------------
    // Processing (Real-Time Safe)
    // -------------------------------------------------------------------------

    /// @brief Forward FFT: real time-domain → complex frequency-domain
    /// @param input N real samples
    /// @param output N/2+1 complex bins (DC to Nyquist)
    /// @pre prepare() has been called
    /// @note Real-time safe, noexcept
    void forward(const float* input, Complex* output) noexcept {
        if (!isPrepared() || input == nullptr || output == nullptr) return;

        const size_t N = size_;

        // Copy input to SIMD-aligned buffer
        std::copy_n(input, N, buf1_.get());

        // SIMD-accelerated forward FFT (ordered output)
        pffft_transform_ordered(setup_.get(), buf1_.get(), buf2_.get(),
                                work_.get(), PFFFT_FORWARD);

        // Convert pffft ordered output to our Complex format:
        //   pffft: [DC_real, Nyquist_real, Re(1), Im(1), Re(2), Im(2), ...]
        //   ours:  Complex[0]={DC,0}, Complex[k]={Re,Im}, Complex[N/2]={Nyq,0}
        const float* fftOut = buf2_.get();

        output[0] = {fftOut[0], 0.0f};        // DC bin (real only)
        output[N / 2] = {fftOut[1], 0.0f};    // Nyquist bin (real only)

        for (size_t k = 1; k < N / 2; ++k) {
            output[k] = {fftOut[2 * k], fftOut[2 * k + 1]};
        }
    }

    /// @brief Inverse FFT: complex frequency-domain → real time-domain
    /// @param input N/2+1 complex bins (DC to Nyquist)
    /// @param output N real samples
    /// @pre prepare() has been called
    /// @note Real-time safe, noexcept
    void inverse(const Complex* input, float* output) noexcept {
        if (!isPrepared() || input == nullptr || output == nullptr) return;

        const size_t N = size_;

        // Convert our Complex format to pffft ordered format
        float* fftIn = buf1_.get();

        fftIn[0] = input[0].real;          // DC
        fftIn[1] = input[N / 2].real;      // Nyquist

        for (size_t k = 1; k < N / 2; ++k) {
            fftIn[2 * k] = input[k].real;
            fftIn[2 * k + 1] = input[k].imag;
        }

        // SIMD-accelerated inverse FFT (ordered input)
        pffft_transform_ordered(setup_.get(), fftIn, buf2_.get(),
                                work_.get(), PFFFT_BACKWARD);

        // Copy and normalize (pffft inverse is unscaled: IFFT(FFT(x)) = N*x)
        const float scale = 1.0f / static_cast<float>(N);
        const float* fftOut = buf2_.get();
        for (size_t i = 0; i < N; ++i) {
            output[i] = fftOut[i] * scale;
        }
    }

    // -------------------------------------------------------------------------
    // Query
    // -------------------------------------------------------------------------

    /// @brief Get configured FFT size
    [[nodiscard]] size_t size() const noexcept { return size_; }

    /// @brief Get number of output bins (N/2+1)
    [[nodiscard]] size_t numBins() const noexcept { return size_ / 2 + 1; }

    /// @brief Check if prepare() has been called
    [[nodiscard]] bool isPrepared() const noexcept { return size_ > 0 && setup_ != nullptr; }

private:
    size_t size_ = 0;
    std::unique_ptr<PFFFT_Setup, detail::PffftSetupDeleter> setup_;
    std::unique_ptr<float, detail::PffftAlignedDeleter> buf1_;  // Input staging
    std::unique_ptr<float, detail::PffftAlignedDeleter> buf2_;  // Output staging
    std::unique_ptr<float, detail::PffftAlignedDeleter> work_;  // pffft work buffer
};

} // namespace DSP
} // namespace Krate
