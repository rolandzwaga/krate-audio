// ==============================================================================
// Layer 1: DSP Primitive - Short-Time Fourier Transform
// ==============================================================================
// STFT for continuous audio stream analysis and OverlapAdd for synthesis.
// Provides streaming spectral processing with configurable windows and overlap.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept process, allocation in prepare())
// - Principle III: Modern C++ (C++20, RAII)
// - Principle IX: Layer 1 (depends only on Layer 0 / fft.h)
// - Principle X: DSP Constraints (COLA windows, proper overlap)
// - Principle XII: Test-First Development
//
// Reference: specs/007-fft-processor/spec.md
// ==============================================================================

#pragma once

#include "fft.h"
#include "spectral_buffer.h"
#include "../core/window_functions.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>


namespace Krate::DSP {

namespace detail {

/// @brief The FFTs of one STFT / OverlapAdd: the prepared size, plus (opt-in)
///        every power of two below it, so the active frame size can change
///        without allocating.
class FFTSizeSet {
public:
    /// Halvings below the prepared size that can be reserved (8192 -> 128).
    static constexpr size_t kMaxReducedSizes = 6;

    /// @brief Prepare the transform for @p fftSize and drop any reduced sizes
    /// @note NOT real-time safe (allocates memory)
    void prepare(size_t fftSize) noexcept {
        ffts_[0].prepare(fftSize);
        for (size_t k = 1; k < ffts_.size(); ++k) {
            ffts_[k] = FFT{};
        }
        active_ = 0;
    }

    /// @brief Prepare every power of two in [minFftSize, prepared size)
    /// @note NOT real-time safe (allocates memory)
    void prepareReducedSizes(size_t minFftSize) noexcept {
        for (size_t k = 1; k < ffts_.size(); ++k) {
            const size_t n = ffts_[0].size() >> k;
            if (n == 0 || n < minFftSize) break;
            if (ffts_[k].size() != n) ffts_[k].prepare(n);
        }
    }

    /// @brief Make the prepared transform of size @p fftSize the active one
    /// @return false (active transform unchanged) if no such size is prepared
    /// @note Real-time safe
    [[nodiscard]] bool select(size_t fftSize) noexcept {
        for (size_t k = 0; k < ffts_.size(); ++k) {
            if (ffts_[k].isPrepared() && ffts_[k].size() == fftSize) {
                active_ = k;
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] FFT& active() noexcept { return ffts_[active_]; }

private:
    std::array<FFT, kMaxReducedSizes + 1> ffts_;  ///< [k] has size (prepared >> k)
    size_t active_ = 0;
};

} // namespace detail

// =============================================================================
// STFT Class
// =============================================================================

/// @brief Short-Time Fourier Transform for continuous audio streams
class STFT {
public:
    STFT() noexcept = default;
    ~STFT() noexcept = default;

    // Non-copyable, movable
    STFT(const STFT&) = delete;
    STFT& operator=(const STFT&) = delete;
    STFT(STFT&&) noexcept = default;
    STFT& operator=(STFT&&) noexcept = default;

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    /// @brief Prepare STFT processor
    /// @param fftSize FFT size (power of 2, 256-8192)
    /// @param hopSize Frame advance in samples (typically fftSize/2 or fftSize/4)
    /// @param window Window type for analysis
    /// @param kaiserBeta Kaiser beta parameter (only used if window == Kaiser)
    /// @pre fftSize is power of 2
    /// @pre hopSize <= fftSize
    /// @note NOT real-time safe (allocates memory)
    void prepare(
        size_t fftSize,
        size_t hopSize,
        WindowType window = WindowType::Hann,
        float kaiserBeta = 9.0f
    ) noexcept {
        fftSize_ = fftSize;
        hopSize_ = hopSize;
        windowType_ = window;
        kaiserBeta_ = kaiserBeta;

        // Prepare internal FFT
        ffts_.prepare(fftSize);

        // Generate window
        window_ = Window::generate(window, fftSize, kaiserBeta);

        // Allocate input buffer
        // Size: fftSize + max expected batch size
        // For batch testing, we need room for multiple frames worth of input
        // Using 8*fftSize allows pushing up to 7*fftSize samples before processing
        inputBuffer_.resize(fftSize * 8, 0.0f);

        // Allocate windowed frame buffer
        windowedFrame_.resize(fftSize, 0.0f);

        // Reset state
        writeIndex_ = 0;
        samplesAvailable_ = 0;
    }

    /// @brief Reserve transforms so setActiveSize() can select any power of two
    ///        in [minFftSize, prepared fftSize] without allocating
    /// @note NOT real-time safe (allocates memory). Call after prepare().
    void prepareActiveSizes(size_t minFftSize) noexcept {
        ffts_.prepareReducedSizes(minFftSize);
    }

    /// @brief Switch the frame size inside the buffers prepare() allocated
    /// @param fftSize prepare()'s size, or a smaller size reserved by prepareActiveSizes()
    /// @param hopSize New frame advance (0 < hopSize <= fftSize)
    /// @return false (nothing changed) if @p fftSize is not available
    /// @note Real-time safe: no allocation. Regenerates the window in place and
    ///       clears the accumulated input, like reset().
    [[nodiscard]] bool setActiveSize(size_t fftSize, size_t hopSize) noexcept {
        if (hopSize == 0 || hopSize > fftSize || !ffts_.select(fftSize)) return false;
        fftSize_ = fftSize;
        hopSize_ = hopSize;
        Window::generateInto(window_.data(), windowType_, fftSize, kaiserBeta_);
        reset();
        return true;
    }

    /// @brief Reset internal buffers (clear accumulated samples)
    /// @note Real-time safe
    void reset() noexcept {
        std::fill(inputBuffer_.begin(), inputBuffer_.end(), 0.0f);
        writeIndex_ = 0;
        samplesAvailable_ = 0;
    }

    // -------------------------------------------------------------------------
    // Input (Real-Time Safe)
    // -------------------------------------------------------------------------

    /// @brief Push samples into input buffer
    /// @param input Input samples
    /// @param numSamples Number of samples
    /// @note Real-time safe, noexcept
    void pushSamples(const float* input, size_t numSamples) noexcept {
        if (input == nullptr || !isPrepared()) return;

        // Copy samples into circular buffer, in at most two CONTIGUOUS runs.
        //
        // The obvious form is a per-sample `writeIndex_ = (writeIndex_ + 1) %
        // inputBuffer_.size()`, and it is a hardware integer divide per sample:
        // `%` by a runtime value cannot be strength-reduced, and it does not
        // vectorise. The ring wraps at most once per call, so splitting the copy
        // at the wrap point removes every divide and leaves two memcpy-shaped
        // loops. Exact, and it assumes nothing about the buffer length.
        const size_t bufSize = inputBuffer_.size();
        size_t offset = 0;
        while (offset < numSamples) {
            const size_t run = std::min(numSamples - offset, bufSize - writeIndex_);
            std::copy_n(input + offset, run, inputBuffer_.begin() + static_cast<std::ptrdiff_t>(writeIndex_));
            writeIndex_ += run;
            if (writeIndex_ == bufSize) {
                writeIndex_ = 0;
            }
            offset += run;
        }
        samplesAvailable_ += numSamples;
    }

    // -------------------------------------------------------------------------
    // Analysis (Real-Time Safe)
    // -------------------------------------------------------------------------

    /// @brief Check if enough samples for analysis frame
    [[nodiscard]] bool canAnalyze() const noexcept {
        // Need at least fftSize samples for the first frame
        // After that, need hopSize new samples for each subsequent frame
        return samplesAvailable_ >= fftSize_;
    }

    /// @brief Perform windowed FFT analysis
    /// @param output SpectralBuffer to receive spectrum
    /// @pre canAnalyze() returns true
    /// @note Real-time safe, noexcept
    void analyze(SpectralBuffer& output) noexcept {
        if (!canAnalyze() || !output.isPrepared()) return;

        // Calculate read position (start of current frame)
        // We want to read the oldest fftSize samples
        const size_t bufSize = inputBuffer_.size();
        size_t readIdx = (writeIndex_ + bufSize - samplesAvailable_) % bufSize;

        // Extract and window the frame, in at most two CONTIGUOUS runs.
        //
        // The per-sample `readIdx = (readIdx + 1) % bufSize` this replaces was a
        // hardware integer divide per FFT bin-time sample - fftSize_ of them per
        // analysis frame, and the loop could not vectorise around it. The read
        // wraps at most once inside one frame (bufSize >= 8 * fftSize_), so the
        // split is exact and assumes nothing about the buffer length.
        const size_t first = std::min(fftSize_, bufSize - readIdx);
        for (size_t i = 0; i < first; ++i) {
            windowedFrame_[i] = inputBuffer_[readIdx + i] * window_[i];
        }
        for (size_t i = first; i < fftSize_; ++i) {
            windowedFrame_[i] = inputBuffer_[i - first] * window_[i];
        }

        // Perform FFT
        ffts_.active().forward(windowedFrame_.data(), output.data());

        // Consume hopSize samples (advance by hop)
        samplesAvailable_ -= hopSize_;
    }

    // -------------------------------------------------------------------------
    // Query
    // -------------------------------------------------------------------------

    [[nodiscard]] size_t fftSize() const noexcept { return fftSize_; }
    [[nodiscard]] size_t hopSize() const noexcept { return hopSize_; }
    [[nodiscard]] WindowType windowType() const noexcept { return windowType_; }

    /// @brief Processing latency in samples (equals fftSize)
    [[nodiscard]] size_t latency() const noexcept { return fftSize_; }

    [[nodiscard]] bool isPrepared() const noexcept { return fftSize_ > 0; }

private:
    detail::FFTSizeSet ffts_;
    std::vector<float> window_;
    std::vector<float> inputBuffer_;
    std::vector<float> windowedFrame_;
    WindowType windowType_ = WindowType::Hann;
    float kaiserBeta_ = kDefaultKaiserBeta;
    size_t fftSize_ = 0;
    size_t hopSize_ = 0;
    size_t writeIndex_ = 0;
    size_t samplesAvailable_ = 0;
};

// =============================================================================
// OverlapAdd Class
// =============================================================================

/// @brief Overlap-Add synthesis for STFT reconstruction
class OverlapAdd {
public:
    OverlapAdd() noexcept = default;
    ~OverlapAdd() noexcept = default;

    // Non-copyable, movable
    OverlapAdd(const OverlapAdd&) = delete;
    OverlapAdd& operator=(const OverlapAdd&) = delete;
    OverlapAdd(OverlapAdd&&) noexcept = default;
    OverlapAdd& operator=(OverlapAdd&&) noexcept = default;

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    /// @brief Prepare synthesis processor
    /// @param fftSize FFT size (must match STFT)
    /// @param hopSize Frame advance (must match STFT)
    /// @param window Window type (for COLA normalization)
    /// @param kaiserBeta Kaiser beta parameter (only used if window == Kaiser)
    /// @param applySynthesisWindow If true, apply synthesis window to IFFT output
    ///        before overlap-add. Required for spectral modification processors
    ///        (e.g. phase vocoder pitch shifting) at >=75% overlap where Hann²
    ///        satisfies COLA. Must NOT be used at 50% overlap (Hann² not COLA).
    /// @note NOT real-time safe (allocates memory)
    void prepare(
        size_t fftSize,
        size_t hopSize,
        WindowType window = WindowType::Hann,
        float kaiserBeta = 9.0f,
        bool applySynthesisWindow = false
    ) noexcept {
        fftSize_ = fftSize;
        hopSize_ = hopSize;
        applySynthesisWindow_ = applySynthesisWindow;
        windowType_ = window;
        kaiserBeta_ = kaiserBeta;

        // Prepare internal FFT
        ffts_.prepare(fftSize);

        // Generate synthesis window and compute COLA normalization factor
        synthesisWindow_ = Window::generate(window, fftSize, kaiserBeta);
        updateColaNormalization();

        // Output buffer needs to hold at least fftSize + hopSize for overlap-add
        // We use 2*fftSize for safe margin
        outputBuffer_.resize(fftSize * 2, 0.0f);

        // IFFT result buffer
        ifftBuffer_.resize(fftSize, 0.0f);

        // Reset state
        samplesReady_ = 0;
    }

    /// @brief Reserve transforms so setActiveSize() can select any power of two
    ///        in [minFftSize, prepared fftSize] without allocating
    /// @note NOT real-time safe (allocates memory). Call after prepare().
    void prepareActiveSizes(size_t minFftSize) noexcept {
        ffts_.prepareReducedSizes(minFftSize);
    }

    /// @brief Switch the frame size inside the buffers prepare() allocated
    /// @param fftSize prepare()'s size, or a smaller size reserved by prepareActiveSizes()
    /// @param hopSize New frame advance (must match the STFT)
    /// @return false (nothing changed) if @p fftSize is not available
    /// @note Real-time safe: no allocation. Regenerates the synthesis window in
    ///       place, recomputes the COLA normalisation and clears the accumulator.
    [[nodiscard]] bool setActiveSize(size_t fftSize, size_t hopSize) noexcept {
        if (hopSize == 0 || hopSize > fftSize || !ffts_.select(fftSize)) return false;
        fftSize_ = fftSize;
        hopSize_ = hopSize;
        Window::generateInto(synthesisWindow_.data(), windowType_, fftSize, kaiserBeta_);
        updateColaNormalization();
        reset();
        return true;
    }

    /// @brief Reset output accumulator
    /// @note Real-time safe
    void reset() noexcept {
        std::fill(outputBuffer_.begin(), outputBuffer_.end(), 0.0f);
        samplesReady_ = 0;
        readOffset_ = 0;
    }

    // -------------------------------------------------------------------------
    // Synthesis (Real-Time Safe)
    // -------------------------------------------------------------------------

    /// @brief Add IFFT frame to output accumulator
    /// @param input SpectralBuffer containing spectrum to synthesize
    /// @note Real-time safe, noexcept
    void synthesize(const SpectralBuffer& input) noexcept {
        if (!isPrepared() || !input.isPrepared()) return;

        // The frame accumulates at the read position: move the unread samples there first
        compact();

        // Perform inverse FFT
        ffts_.active().inverse(input.data(), ifftBuffer_.data());

        // Overlap-add: Add IFFT result to output buffer with COLA normalization
        // When applySynthesisWindow_ is true, we apply the window to the IFFT output
        // before accumulation. This is essential for spectral modification (e.g. PV
        // pitch shifting) where the IFFT output no longer has the smooth taper of
        // the original analysis window, causing boundary discontinuities.
        if (applySynthesisWindow_) {
            for (size_t i = 0; i < fftSize_; ++i) {
                outputBuffer_[i] += ifftBuffer_[i] * synthesisWindow_[i] * colaNormalization_;
            }
        } else {
            for (size_t i = 0; i < fftSize_; ++i) {
                outputBuffer_[i] += ifftBuffer_[i] * colaNormalization_;
            }
        }

        // Mark hopSize more samples as ready
        samplesReady_ += hopSize_;
    }

    // -------------------------------------------------------------------------
    // Output (Real-Time Safe)
    // -------------------------------------------------------------------------

    /// @brief Get number of samples available to pull
    [[nodiscard]] size_t samplesAvailable() const noexcept {
        return samplesReady_;
    }

    /// @brief Extract output samples from accumulator
    /// @param output Destination buffer
    /// @param numSamples Number of samples to extract
    /// @pre numSamples <= samplesAvailable()
    /// @pre numSamples <= 2 * fftSize() (the accumulator's physical size)
    /// @note Real-time safe, noexcept
    void pullSamples(float* output, size_t numSamples) noexcept {
        // The capacity term is not redundant with samplesReady_: synthesize()
        // accumulates every frame at offset 0 and only ever ADDS hopSize to
        // samplesReady_, so a caller that synthesizes several frames without
        // pulling between them can drive samplesReady_ past outputBuffer_.size().
        // Honouring such a pull would read past the end and - via the "zero the
        // freed portion" fill below - write BEFORE the buffer, i.e. corrupt the
        // heap. Pull one hop per synthesize (see the class docs) and this is
        // unreachable; the guard exists so that getting it wrong is inert rather
        // than undefined.
        //
        // A pull reads at readOffset_ and only advances it; the unread samples move to the front
        // (compact()) when the next frame is added or a pull would run past the active region.
        // The samples read are the ones a shift per pull would read: shifting by a then by b
        // is shifting by a + b. Shifting the whole active region on every pull cost
        // O(fftSize) per sample for a caller pulling one sample at a time (Disrumpo's Spectral
        // node: 78 % of a core for one band, preset All_Types_Morph over 100 %).
        const size_t activeLength = std::min(outputBuffer_.size(), fftSize_ * 2);
        if (output == nullptr || numSamples > samplesReady_
            || numSamples > activeLength) {
            return;
        }
        if (readOffset_ + numSamples > activeLength) {
            compact();
        }

        // Copy samples to output
        const auto from = outputBuffer_.begin() + static_cast<std::ptrdiff_t>(readOffset_);
        std::copy(from, from + static_cast<std::ptrdiff_t>(numSamples), output);

        readOffset_ += numSamples;
        samplesReady_ -= numSamples;
    }

    // -------------------------------------------------------------------------
    // Query
    // -------------------------------------------------------------------------

    [[nodiscard]] size_t fftSize() const noexcept { return fftSize_; }
    [[nodiscard]] size_t hopSize() const noexcept { return hopSize_; }
    [[nodiscard]] bool isPrepared() const noexcept { return fftSize_ > 0; }

private:
    /// @brief The samples pulled since the last compaction leave the accumulator: the active
    /// region [0, 2 * fftSize_) shifts left by readOffset_ and its last readOffset_ samples are
    /// zeroed (only the active region: after setActiveSize() to a smaller size everything past it
    /// stays zero, as reset() cleared it and synthesize() writes [0, fftSize_) only).
    void compact() noexcept {
        if (readOffset_ == 0) return;
        const size_t activeLength = std::min(outputBuffer_.size(), fftSize_ * 2);
        const auto activeEnd = outputBuffer_.begin() + static_cast<std::ptrdiff_t>(activeLength);
        std::copy(outputBuffer_.begin() + static_cast<std::ptrdiff_t>(readOffset_), activeEnd,
                  outputBuffer_.begin());
        std::fill(activeEnd - static_cast<std::ptrdiff_t>(readOffset_), activeEnd, 0.0f);
        readOffset_ = 0;
    }

    /// @brief COLA normalisation for the current window, size and hop
    /// Without synthesis window: COLA sum = sum of w[k] at hop positions.
    /// With synthesis window: COLA sum = sum of w[k]² at hop positions (Hann²).
    void updateColaNormalization() noexcept {
        const size_t numOverlaps = (fftSize_ + hopSize_ - 1) / hopSize_;
        float colaSum = 0.0f;
        for (size_t frame = 0; frame < numOverlaps; ++frame) {
            const size_t idx = frame * hopSize_;
            if (idx < fftSize_) {
                if (applySynthesisWindow_) {
                    colaSum += synthesisWindow_[idx] * synthesisWindow_[idx];
                } else {
                    colaSum += synthesisWindow_[idx];
                }
            }
        }
        // Normalization: divide by COLA sum to get unity gain reconstruction
        colaNormalization_ = (colaSum > 0.0f) ? (1.0f / colaSum) : 1.0f;
    }

    detail::FFTSizeSet ffts_;
    std::vector<float> synthesisWindow_;
    std::vector<float> outputBuffer_;
    std::vector<float> ifftBuffer_;
    float colaNormalization_ = 1.0f;
    float kaiserBeta_ = kDefaultKaiserBeta;
    WindowType windowType_ = WindowType::Hann;
    bool applySynthesisWindow_ = false;
    size_t fftSize_ = 0;
    size_t hopSize_ = 0;
    size_t samplesReady_ = 0;
    size_t readOffset_ = 0;  ///< samples pulled since the last compact()
};

} // namespace Krate::DSP

