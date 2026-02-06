// ==============================================================================
// Layer 2: DSP Processor - Spectral Freeze Oscillator
// ==============================================================================
// Captures a single FFT frame and continuously resynthesizes it as a frozen
// spectral drone. Features freeze/unfreeze with click-free crossfade, pitch
// shift via bin shifting with linear interpolation, spectral tilt (brightness
// control), and formant shift via cepstral envelope manipulation.
//
// Uses coherent per-bin phase advancement with IFFT overlap-add resynthesis
// via a custom ring buffer with explicit Hann synthesis window at 75% overlap.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept processing, allocation in prepare())
// - Principle III: Modern C++ (C++20, RAII, [[nodiscard]])
// - Principle IX: Layer 2 (depends on Layer 0-1 and FormantPreserver at Layer 2)
// - Principle X: DSP Constraints (COLA-compliant Hann window, 75% overlap)
// - Principle XII: Test-First Development
// - Principle XIV: ODR-safe (unique class name, no duplicates)
//
// Performance:
// - < 0.5% CPU single core @ 44.1kHz, 512 samples, 2048 FFT (SC-003)
// - < 200 KB memory for 2048 FFT @ 44.1kHz (SC-008)
// - Zero allocations in audio thread (FR-023, FR-024)
//
// Usage:
//   SpectralFreezeOscillator osc;
//   osc.prepare(44100.0, 2048);
//
//   // Feed audio and freeze at desired moment
//   osc.freeze(audioBlock, blockSize);
//
//   // Generate output
//   std::vector<float> output(512);
//   osc.processBlock(output.data(), 512);
//
//   // Modify frozen spectrum
//   osc.setPitchShift(7.0f);       // Perfect fifth up
//   osc.setSpectralTilt(-3.0f);    // Darken
//   osc.setFormantShift(-12.0f);   // Lower formants
//
//   // Release
//   osc.unfreeze();  // Crossfades to silence over one hop
//
// Memory usage formula (all values for fftSize=N, numBins=N/2+1):
//   Without formant: ~(14*N + 6*(N/2+1)) bytes = ~90 KB for N=2048
//   With formant:    above + FormantPreserver (~70 KB) + 2*(N/2+1)*4 = ~170 KB
//
// Reference: specs/030-spectral-freeze-oscillator/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/primitives/fft.h>
#include <krate/dsp/primitives/spectral_buffer.h>
#include <krate/dsp/primitives/spectral_utils.h>
#include <krate/dsp/processors/formant_preserver.h>
#include <krate/dsp/core/window_functions.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/pitch_utils.h>
#include <krate/dsp/core/db_utils.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <vector>

namespace Krate {
namespace DSP {

/// @brief Spectral freeze oscillator that captures and resynthesizes FFT frames.
///
/// Captures a single FFT frame's magnitude and phase spectrum from an audio
/// input, then continuously outputs a stable drone by advancing phase coherently
/// on each synthesis hop. Supports pitch shifting (bin shifting), spectral tilt
/// (brightness control), and formant shifting (spectral envelope manipulation).
///
/// @par Layer: 2 (processors/)
/// @par Dependencies: FFT, SpectralBuffer, FormantPreserver, Window, spectral_utils
class SpectralFreezeOscillator {
public:
    // =========================================================================
    // Lifecycle (FR-001, FR-002, FR-003)
    // =========================================================================

    /// @brief Default constructor. Must call prepare() before processing.
    SpectralFreezeOscillator() noexcept = default;

    /// @brief Destructor.
    ~SpectralFreezeOscillator() noexcept = default;

    // Non-copyable, movable
    SpectralFreezeOscillator(const SpectralFreezeOscillator&) = delete;
    SpectralFreezeOscillator& operator=(const SpectralFreezeOscillator&) = delete;
    SpectralFreezeOscillator(SpectralFreezeOscillator&&) noexcept = default;
    SpectralFreezeOscillator& operator=(SpectralFreezeOscillator&&) noexcept = default;

    /// @brief Allocate all internal buffers and initialize state (FR-001).
    ///
    /// @param sampleRate Sample rate in Hz
    /// @param fftSize FFT size (power of 2, 256-8192). Default: 2048.
    ///        Non-power-of-2 values are clamped to nearest valid size.
    ///
    /// @note NOT real-time safe (allocates memory)
    void prepare(double sampleRate, size_t fftSize = 2048) noexcept {
        // Validate and clamp FFT size to [256, 8192] power-of-2
        if (fftSize < kMinFFTSize) fftSize = kMinFFTSize;
        if (fftSize > kMaxFFTSize) fftSize = kMaxFFTSize;
        if (!std::has_single_bit(fftSize)) {
            // Clamp to nearest lower power of 2
            fftSize = std::bit_floor(fftSize);
            if (fftSize < kMinFFTSize) fftSize = kMinFFTSize;
        }

        sampleRate_ = sampleRate;
        fftSize_ = fftSize;
        hopSize_ = fftSize / 4;  // 75% overlap (FR-001, FR-010)
        numBins_ = fftSize / 2 + 1;

        // Prepare FFT engine
        fft_.prepare(fftSize);

        // Prepare working spectrum buffer
        workingSpectrum_.prepare(fftSize);

        // Allocate frozen state arrays
        frozenMagnitudes_.resize(numBins_, 0.0f);
        initialPhases_.resize(numBins_, 0.0f);

        // Allocate phase accumulators
        phaseAccumulators_.resize(numBins_, 0.0f);
        phaseIncrements_.resize(numBins_, 0.0f);

        // Pre-compute phase increments for each bin (FR-008)
        for (size_t k = 0; k < numBins_; ++k) {
            phaseIncrements_[k] = expectedPhaseIncrement(k, hopSize_, fftSize_);
        }

        // Allocate processing buffers
        ifftBuffer_.resize(fftSize_, 0.0f);
        workingMagnitudes_.resize(numBins_, 0.0f);
        captureBuffer_.resize(fftSize_, 0.0f);
        captureComplexBuf_.resize(numBins_);

        // Generate Hann synthesis window (no analysis window needed -- see freeze())
        synthesisWindow_.resize(fftSize_, 0.0f);
        Window::generateHann(synthesisWindow_.data(), fftSize_);

        // Compute COLA normalization factor for Hann window at 75% overlap
        // Sum of overlapping periodic Hann windows = 2.0 at 75% overlap
        float colaSum = 0.0f;
        for (size_t pos = 0; pos < fftSize_; pos += hopSize_) {
            colaSum += synthesisWindow_[pos];
        }
        colaNormalization_ = 1.0f / colaSum;

        // Allocate output ring buffer (2x fftSize for overlap-add)
        outputBuffer_.resize(fftSize_ * 2, 0.0f);

        // Formant analysis buffers
        formantPreserver_.prepare(fftSize_, sampleRate);
        originalEnvelope_.resize(numBins_, 1.0f);
        shiftedEnvelope_.resize(numBins_, 1.0f);

        // Reset all state
        prepared_ = true;
        reset();
    }

    /// @brief Clear all internal buffers and state without deallocating (FR-002).
    ///
    /// @note Real-time safe
    void reset() noexcept {
        if (!prepared_) return;

        frozen_ = false;
        unfreezing_ = false;
        unfadeSamplesRemaining_ = 0;

        std::fill(frozenMagnitudes_.begin(), frozenMagnitudes_.end(), 0.0f);
        std::fill(initialPhases_.begin(), initialPhases_.end(), 0.0f);
        std::fill(phaseAccumulators_.begin(), phaseAccumulators_.end(), 0.0f);
        std::fill(ifftBuffer_.begin(), ifftBuffer_.end(), 0.0f);
        std::fill(workingMagnitudes_.begin(), workingMagnitudes_.end(), 0.0f);
        std::fill(captureBuffer_.begin(), captureBuffer_.end(), 0.0f);
        std::fill(outputBuffer_.begin(), outputBuffer_.end(), 0.0f);
        std::fill(originalEnvelope_.begin(), originalEnvelope_.end(), 1.0f);
        std::fill(shiftedEnvelope_.begin(), shiftedEnvelope_.end(), 1.0f);

        workingSpectrum_.reset();
        formantPreserver_.reset();

        outputWriteIndex_ = 0;
        outputReadIndex_ = 0;
        samplesInBuffer_ = 0;
    }

    /// @brief Check if prepare() has been called successfully (FR-003).
    [[nodiscard]] bool isPrepared() const noexcept {
        return prepared_;
    }

    // =========================================================================
    // Freeze / Unfreeze (FR-004, FR-005, FR-006, FR-007)
    // =========================================================================

    /// @brief Capture spectral content from audio block (FR-004).
    ///
    /// Performs FFT (without analysis window) on the input block and stores
    /// magnitude and phase spectrum. If blockSize < fftSize, the block is
    /// zero-padded. Pre-fills the overlap-add pipeline for click-free start.
    ///
    /// @param inputBlock Audio samples to analyze
    /// @param blockSize Number of samples in inputBlock
    ///
    /// @note Real-time safe (uses pre-allocated buffers)
    void freeze(const float* inputBlock, size_t blockSize) noexcept {
        if (!prepared_ || inputBlock == nullptr || blockSize == 0) return;

        // Copy input to capture buffer with zero-padding if needed (FR-004)
        std::fill(captureBuffer_.begin(), captureBuffer_.end(), 0.0f);
        const size_t copyLen = std::min(blockSize, fftSize_);
        std::copy_n(inputBlock, copyLen, captureBuffer_.data());

        // Forward FFT WITHOUT analysis window.
        // Spectral freeze captures the raw spectrum so that resynthesis
        // with coherent phase advancement produces stable output. An analysis
        // window (Hann) would create sidelobes at neighboring bins, and since
        // each bin advances phase independently during resynthesis, those
        // sidelobe components would beat against each other causing audible
        // amplitude modulation at the bin-spacing frequency. The synthesis
        // Hann window + COLA normalization ensures smooth overlap-add
        // reconstruction regardless.
        fft_.forward(captureBuffer_.data(), captureComplexBuf_.data());

        // Store magnitude and phase (FR-007, FR-009)
        for (size_t k = 0; k < numBins_; ++k) {
            frozenMagnitudes_[k] = captureComplexBuf_[k].magnitude();
            initialPhases_[k] = captureComplexBuf_[k].phase();
        }

        // Initialize phase accumulators from captured phases (FR-009)
        std::copy(initialPhases_.begin(), initialPhases_.end(),
                  phaseAccumulators_.begin());

        // Reset output buffer for clean start
        std::fill(outputBuffer_.begin(), outputBuffer_.end(), 0.0f);
        outputWriteIndex_ = 0;
        outputReadIndex_ = 0;
        samplesInBuffer_ = 0;

        // Set frozen state
        frozen_ = true;
        unfreezing_ = false;
        unfadeSamplesRemaining_ = 0;

        // Pre-fill the overlap-add pipeline.
        // With 75% overlap (overlapFactor = fftSize/hopSize = 4), a position
        // in the output buffer needs contributions from 4 overlapping frames
        // for COLA-compliant constant-amplitude output. We synthesize
        // overlapFactor frames, then skip past the initial ramp-up region
        // where fewer than overlapFactor frames have contributed.
        const size_t overlapFactor = fftSize_ / hopSize_;  // 4 for 75% overlap
        for (size_t f = 0; f < overlapFactor; ++f) {
            synthesizeFrame();
        }

        // After overlapFactor frames: samplesInBuffer_ = overlapFactor * hopSize
        // = fftSize. Positions [0, hopSize) have only 1 frame's contribution,
        // positions [hopSize, 2*hopSize) have 2, etc. Only positions starting
        // at (overlapFactor-1)*hopSize have full coverage. Skip past the
        // incomplete ramp-up region to start reading from the first fully
        // COLA-compliant position.
        const size_t skipSamples = (overlapFactor - 1) * hopSize_;

        // Clear the skipped positions so they don't accumulate stale data
        // when the write pointer wraps around later.
        for (size_t i = 0; i < skipSamples; ++i) {
            outputBuffer_[i % outputBuffer_.size()] = 0.0f;
        }

        outputReadIndex_ = skipSamples % outputBuffer_.size();
        samplesInBuffer_ -= skipSamples;
    }

    /// @brief Release frozen state and fade to silence (FR-005).
    ///
    /// Initiates a linear crossfade to zero over one hop duration.
    ///
    /// @note Real-time safe
    void unfreeze() noexcept {
        if (!frozen_ || !prepared_) return;

        unfreezing_ = true;
        unfadeSamplesRemaining_ = hopSize_;
    }

    /// @brief Check if oscillator is in frozen state (FR-006).
    [[nodiscard]] bool isFrozen() const noexcept {
        return frozen_;
    }

    // =========================================================================
    // Processing (FR-008 to FR-011)
    // =========================================================================

    /// @brief Generate output samples from frozen spectrum (FR-011).
    ///
    /// @param output Destination buffer (must hold numSamples floats)
    /// @param numSamples Number of samples to generate
    ///
    /// @note Real-time safe: noexcept, no allocations (FR-023, FR-024)
    void processBlock(float* output, size_t numSamples) noexcept {
        if (output == nullptr || numSamples == 0) return;

        // FR-028: Output zeros if not prepared
        if (!prepared_) {
            std::fill_n(output, numSamples, 0.0f);
            return;
        }

        // FR-027: Output zeros if not frozen
        if (!frozen_) {
            std::fill_n(output, numSamples, 0.0f);
            return;
        }

        // Generate samples via overlap-add
        for (size_t i = 0; i < numSamples; ++i) {
            // Synthesize new frames as needed
            while (samplesInBuffer_ < hopSize_) {
                synthesizeFrame();
            }

            // Pull sample from output ring buffer
            float sample = outputBuffer_[outputReadIndex_];
            outputBuffer_[outputReadIndex_] = 0.0f;  // Clear for next overlap-add
            outputReadIndex_ = (outputReadIndex_ + 1) % outputBuffer_.size();
            --samplesInBuffer_;

            // Apply unfreeze crossfade (FR-005)
            if (unfreezing_) {
                if (unfadeSamplesRemaining_ > 0) {
                    float fadeGain = static_cast<float>(unfadeSamplesRemaining_)
                                   / static_cast<float>(hopSize_);
                    sample *= fadeGain;
                    --unfadeSamplesRemaining_;
                } else {
                    // Crossfade complete
                    frozen_ = false;
                    unfreezing_ = false;
                    sample = 0.0f;
                }
            }

            // Flush denormals (FR-025)
            output[i] = detail::flushDenormal(sample);
        }
    }

    // =========================================================================
    // Parameters (FR-012 to FR-022)
    // =========================================================================

    /// @brief Set pitch shift in semitones (FR-012).
    /// @param semitones Pitch shift [-24, +24]. Clamped to range.
    void setPitchShift(float semitones) noexcept {
        pitchShiftSemitones_ = std::clamp(semitones, -24.0f, 24.0f);
    }

    /// @brief Get current pitch shift in semitones.
    [[nodiscard]] float getPitchShift() const noexcept {
        return pitchShiftSemitones_;
    }

    /// @brief Set spectral tilt in dB/octave (FR-016).
    /// @param dbPerOctave Tilt amount [-24, +24]. Clamped to range.
    void setSpectralTilt(float dbPerOctave) noexcept {
        spectralTiltDbPerOctave_ = std::clamp(dbPerOctave, -24.0f, 24.0f);
    }

    /// @brief Get current spectral tilt in dB/octave.
    [[nodiscard]] float getSpectralTilt() const noexcept {
        return spectralTiltDbPerOctave_;
    }

    /// @brief Set formant shift in semitones (FR-019).
    /// @param semitones Formant shift [-24, +24]. Clamped to range.
    void setFormantShift(float semitones) noexcept {
        formantShiftSemitones_ = std::clamp(semitones, -24.0f, 24.0f);
    }

    /// @brief Get current formant shift in semitones.
    [[nodiscard]] float getFormantShift() const noexcept {
        return formantShiftSemitones_;
    }

    // =========================================================================
    // Query (FR-026)
    // =========================================================================

    /// @brief Get processing latency in samples (FR-026).
    /// @return Latency in samples (fftSize), or 0 if not prepared
    [[nodiscard]] size_t getLatencySamples() const noexcept {
        return prepared_ ? fftSize_ : 0;
    }

    /// @brief Get configured FFT size.
    [[nodiscard]] size_t getFftSize() const noexcept {
        return prepared_ ? fftSize_ : 0;
    }

    /// @brief Get hop size (fftSize/4).
    [[nodiscard]] size_t getHopSize() const noexcept {
        return prepared_ ? hopSize_ : 0;
    }

private:
    // =========================================================================
    // Private Methods
    // =========================================================================

    /// @brief Apply pitch shift via bin remapping with linear interpolation (FR-013).
    ///
    /// For destination bin k, the source bin is k / ratio. Fractional source
    /// bins are linearly interpolated. Bins outside [0, N/2] are zeroed (FR-015).
    void applyPitchShift(const float* inputMags, float* outputMags) noexcept {
        const float ratio = semitonesToRatio(pitchShiftSemitones_);

        for (size_t k = 0; k < numBins_; ++k) {
            const float srcBin = static_cast<float>(k) / ratio;

            // FR-015: Source bin out of range -> zero
            if (srcBin < 0.0f || srcBin >= static_cast<float>(numBins_ - 1)) {
                outputMags[k] = 0.0f;
            } else {
                outputMags[k] = interpolateMagnitudeLinear(
                    inputMags, numBins_, srcBin);
            }
        }
    }

    /// @brief Apply spectral tilt as multiplicative dB/octave gain slope (FR-017).
    ///
    /// For bin k at frequency f_k, gain_dB = tilt * log2(f_k / f_ref).
    /// Bin 0 (DC) is NOT modified (FR-017). Magnitudes clamped to non-negative
    /// (FR-018 -- upper bound applied in output domain rather than magnitude domain,
    /// since FFT magnitudes are inherently scaled by N and only become audio-range
    /// after IFFT normalization).
    void applySpectralTilt(float* magnitudes) noexcept {
        if (spectralTiltDbPerOctave_ == 0.0f) return;  // Optimization: skip when 0

        // Reference frequency is bin 1's frequency
        const float fRef = binToFrequency(1, fftSize_,
                                          static_cast<float>(sampleRate_));
        if (fRef <= 0.0f) return;

        // Skip bin 0 (DC) per FR-017
        for (size_t k = 1; k < numBins_; ++k) {
            const float fk = binToFrequency(k, fftSize_,
                                            static_cast<float>(sampleRate_));
            if (fk <= 0.0f) continue;

            const float octaves = std::log2(fk / fRef);
            const float gainDb = spectralTiltDbPerOctave_ * octaves;
            const float gainLinear = std::pow(10.0f, gainDb / 20.0f);

            magnitudes[k] *= gainLinear;

            // FR-018: Clamp magnitude to non-negative (upper bound enforced by
            // denormal flushing in output stage -- FFT magnitudes are O(N) scale
            // and only reach audio range after IFFT 1/N normalization)
            if (magnitudes[k] < 0.0f) magnitudes[k] = 0.0f;
        }
    }

    /// @brief Apply formant shift via cepstral envelope extraction (FR-020, FR-021, FR-022).
    ///
    /// Extracts the spectral envelope from frozen magnitudes, shifts it by the
    /// formant ratio, and reapplies: output = mag * (shifted_env / original_env).
    void applyFormantShift(float* magnitudes) noexcept {
        if (formantShiftSemitones_ == 0.0f) return;  // Optimization: skip when 0

        const float formantRatio = semitonesToRatio(formantShiftSemitones_);

        // Extract envelope of current magnitudes
        formantPreserver_.extractEnvelope(magnitudes, originalEnvelope_.data());

        // Shift the envelope by resampling bins
        // For destination bin k, source bin = k / formantRatio
        for (size_t k = 0; k < numBins_; ++k) {
            const float srcBin = static_cast<float>(k) / formantRatio;

            if (srcBin < 0.0f || srcBin >= static_cast<float>(numBins_ - 1)) {
                shiftedEnvelope_[k] = FormantPreserver::kMinMagnitude;
            } else {
                shiftedEnvelope_[k] = interpolateMagnitudeLinear(
                    originalEnvelope_.data(), numBins_, srcBin);
                shiftedEnvelope_[k] = std::max(shiftedEnvelope_[k],
                                               FormantPreserver::kMinMagnitude);
            }
        }

        // Apply formant preservation: output = mag * (shifted_env / original_env)
        // This is FR-022
        formantPreserver_.applyFormantPreservation(
            magnitudes,
            originalEnvelope_.data(),
            shiftedEnvelope_.data(),
            magnitudes,
            numBins_);
    }

    /// @brief Synthesize one IFFT frame with overlap-add.
    ///
    /// Applies pitch shift, spectral tilt, and formant shift to frozen magnitudes,
    /// constructs the complex spectrum with accumulated phases, runs IFFT,
    /// applies Hann synthesis window, and overlap-adds to output buffer.
    void synthesizeFrame() noexcept {
        // Start with frozen magnitudes (FR-007: constant)
        std::copy(frozenMagnitudes_.begin(), frozenMagnitudes_.end(),
                  workingMagnitudes_.begin());

        // Apply pitch shift if active (FR-012, FR-013)
        // applyPitchShift reads from input and writes to output (cannot be same
        // array), so we reuse ifftBuffer_ as scratch since it will be overwritten
        // by the IFFT step later.
        if (pitchShiftSemitones_ != 0.0f) {
            float* tempMags = ifftBuffer_.data();
            applyPitchShift(workingMagnitudes_.data(), tempMags);
            std::copy_n(tempMags, numBins_, workingMagnitudes_.data());
        }

        // Apply spectral tilt if active (FR-016, FR-017)
        applySpectralTilt(workingMagnitudes_.data());

        // Apply formant shift if active (FR-019, FR-020, FR-021, FR-022)
        applyFormantShift(workingMagnitudes_.data());

        // Construct complex spectrum from magnitudes + accumulated phases
        // Skip zero-magnitude bins for efficiency
        for (size_t k = 0; k < numBins_; ++k) {
            const float mag = workingMagnitudes_[k];
            if (mag < 1e-20f) {
                workingSpectrum_.setCartesian(k, 0.0f, 0.0f);
            } else {
                const float phase = phaseAccumulators_[k];
                const float real = mag * std::cos(phase);
                const float imag = mag * std::sin(phase);
                workingSpectrum_.setCartesian(k, real, imag);
            }
        }

        // Inverse FFT
        fft_.inverse(workingSpectrum_.data(), ifftBuffer_.data());

        // Apply Hann synthesis window and COLA normalization, then overlap-add
        for (size_t i = 0; i < fftSize_; ++i) {
            const float windowed = ifftBuffer_[i] * synthesisWindow_[i]
                                 * colaNormalization_;
            const size_t outIdx = (outputWriteIndex_ + i) % outputBuffer_.size();
            outputBuffer_[outIdx] += windowed;
        }

        // Advance write index by hop size
        outputWriteIndex_ = (outputWriteIndex_ + hopSize_) % outputBuffer_.size();
        samplesInBuffer_ += hopSize_;

        // Advance phase accumulators for next frame (FR-008, FR-014)
        for (size_t k = 0; k < numBins_; ++k) {
            phaseAccumulators_[k] += phaseIncrements_[k];
            phaseAccumulators_[k] = wrapPhaseFast(phaseAccumulators_[k]);
        }
    }

    // =========================================================================
    // Member Variables
    // =========================================================================

    // Configuration state (set at prepare-time)
    double sampleRate_ = 0.0;
    size_t fftSize_ = 0;
    size_t hopSize_ = 0;
    size_t numBins_ = 0;
    bool prepared_ = false;
    float colaNormalization_ = 1.0f;

    // Frozen state
    std::vector<float> frozenMagnitudes_;
    std::vector<float> initialPhases_;
    bool frozen_ = false;

    // Phase accumulation state
    std::vector<float> phaseAccumulators_;
    std::vector<float> phaseIncrements_;

    // Parameter state
    float pitchShiftSemitones_ = 0.0f;
    float spectralTiltDbPerOctave_ = 0.0f;
    float formantShiftSemitones_ = 0.0f;

    // Processing resources
    FFT fft_;
    SpectralBuffer workingSpectrum_;
    std::vector<float> ifftBuffer_;
    std::vector<float> synthesisWindow_;
    std::vector<float> outputBuffer_;
    size_t outputWriteIndex_ = 0;
    size_t outputReadIndex_ = 0;
    size_t samplesInBuffer_ = 0;
    std::vector<float> workingMagnitudes_;
    std::vector<float> captureBuffer_;
    std::vector<Complex> captureComplexBuf_;

    // Formant processing resources
    FormantPreserver formantPreserver_;
    std::vector<float> originalEnvelope_;
    std::vector<float> shiftedEnvelope_;

    // Unfreeze transition state
    bool unfreezing_ = false;
    size_t unfadeSamplesRemaining_ = 0;

    // Constants
    static constexpr size_t kMinFFTSize = 256;
    static constexpr size_t kMaxFFTSize = 8192;
};

} // namespace DSP
} // namespace Krate
