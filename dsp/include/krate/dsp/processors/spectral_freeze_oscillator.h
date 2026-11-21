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

#include <krate/dsp/core/spectral_simd.h>  // reconstructCartesianBulk (SIMD)
#include <krate/dsp/primitives/fft.h>
#include <krate/dsp/primitives/spectral_buffer.h>
#include <krate/dsp/primitives/spectral_utils.h>
#include <krate/dsp/processors/formant_preserver.h>
#include <krate/dsp/core/window_functions.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/pitch_utils.h>
#include <krate/dsp/core/db_utils.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>


namespace Krate::DSP {

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
        fftSize = std::max(fftSize, kMinFFTSize);
        fftSize = std::min(fftSize, kMaxFFTSize);
        if (!std::has_single_bit(fftSize)) {
            // Clamp to nearest lower power of 2
            fftSize = std::bit_floor(fftSize);
            fftSize = std::max(fftSize, kMinFFTSize);
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

        // Generate Hann synthesis window (no analysis window in freeze(); see comment there)
        synthesisWindow_.resize(fftSize_, 0.0f);
        Window::generateHann(synthesisWindow_.data(), fftSize_);

        // Compute COLA normalization factor for Hann window at 75% overlap
        // Sum of overlapping periodic Hann windows = 2.0 at 75% overlap
        float colaSum = 0.0f;
        for (size_t pos = 0; pos < fftSize_; pos += hopSize_) {
            colaSum += synthesisWindow_[pos];
        }
        colaNormalization_ = 1.0f / colaSum;

        // Allocate output ring buffer (2x fftSize for overlap-add).
        //
        // fftSize_ is a power of two (snapped above), so the ring length is too
        // and its wraparound is a MASK, not a `%`. That matters: the ring is
        // wrapped fftSize_ times per synthesised frame in the overlap-add loop
        // and once per output sample in processBlock(), and `% v.size()` on a
        // runtime value compiles to a hardware integer divide - tens of cycles
        // each, MEASURED at roughly the whole cost of the freeze leg in
        // AtmosphereEngine's SC-004 configuration (d). The mask is exact, not an
        // approximation, for a power-of-two length.
        outputBuffer_.resize(fftSize_ * 2, 0.0f);
        outputMask_ = outputBuffer_.size() - 1;

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
        partialCount_ = 0;
    }

    /// @brief Check if prepare() has been called successfully (FR-003).
    [[nodiscard]] bool isPrepared() const noexcept {
        return prepared_;
    }

    /// @brief Opt into partial resynthesis instead of bin remapping (R-148).
    ///
    /// Off (the default), pitch shift remaps magnitudes between bins and every
    /// bin advances at its bin-centre rate, so a shifted partial always lands
    /// on a bin centre: a note-tracking caller hears up to a semitone of error.
    ///
    /// On, freeze() keeps the spectral peaks (local maxima within 60 dB of the
    /// loudest, at most kMaxPartials, lowest first) with the peak bin's centre as
    /// each partial's true frequency, and every frame synthesises each partial as
    /// an exact sinusoid at frequency * ratio: the frame spectrum is the exact
    /// DFT of the sinusoid's segment, and its phase advances at the shifted
    /// frequency. Exact in pitch for bin-centred input (what a note-tracking
    /// caller freezes); an off-centre partial plays at its bin centre * ratio.
    /// Spectral tilt is applied at each partial's shifted frequency, formant
    /// shift through the envelope at its nearest bin.
    ///
    /// @note Takes effect at the next freeze(); call it before freezing.
    void setPartialResynthesis(bool enabled) noexcept {
        partialMode_ = enabled;
    }

    // =========================================================================
    // Freeze / Unfreeze (FR-004, FR-005, FR-006, FR-007)
    // =========================================================================

    /// @brief Capture spectral content from audio block (FR-004).
    ///
    /// Performs FFT on the input block (without analysis window) and stores
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
        // In standard STFT, a Hann analysis window is applied before FFT. However,
        // for spectral freeze with coherent per-bin phase advancement, an analysis
        // window is counterproductive: the Hann DFT has non-zero coefficients at
        // bins k±1, so even a bin-aligned sinusoid would spread energy to 3 bins.
        // Since each bin advances phase independently, those 3 components produce
        // beating at the bin-spacing frequency (~21 Hz for N=2048 at 44.1kHz).
        // Without an analysis window, bin-aligned frequencies produce energy at
        // exactly one bin, giving stable amplitude. Non-bin-aligned frequencies
        // still produce leakage (inherent to any FFT), but the synthesis Hann
        // window + COLA normalization ensures smooth overlap-add reconstruction.
        fft_.forward(captureBuffer_.data(), captureComplexBuf_.data());

        // Store magnitude and phase (FR-007, FR-009)
        maxFrozenMagnitude_ = 0.0f;
        for (size_t k = 0; k < numBins_; ++k) {
            frozenMagnitudes_[k] = captureComplexBuf_[k].magnitude();
            initialPhases_[k] = captureComplexBuf_[k].phase();
            maxFrozenMagnitude_ = std::max(maxFrozenMagnitude_, frozenMagnitudes_[k]);
        }

        // Initialize phase accumulators from captured phases (FR-009)
        std::copy(initialPhases_.begin(), initialPhases_.end(),
                  phaseAccumulators_.begin());

        if (partialMode_) {
            capturePartials();
        }

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
            outputBuffer_[i & outputMask_] = 0.0f;
        }

        outputReadIndex_ = skipSamples & outputMask_;
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
            outputReadIndex_ = (outputReadIndex_ + 1) & outputMask_;
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

            // FR-018: Clamp output to [-2.0, +2.0] (+6 dB headroom ceiling)
            // Applied in the audio domain where "2.0 linear magnitude" is
            // meaningful. Prevents extreme tilt from causing clipping while
            // keeping spectral processing clean.
            sample = std::clamp(sample, -2.0f, 2.0f);

            // Flush denormals (FR-025)
            output[i] = detail::flushDenormal(sample);
        }
    }

    // =========================================================================
    // Parameters (FR-012 to FR-022)
    // =========================================================================

    /// @brief Set pitch shift in semitones (FR-012).
    ///
    /// The range spans four octaves each way so that callers driving this from a
    /// played note (rather than as a fixed offset) can reach the whole keyboard
    /// from a fixed frozen reference. Shifting is bin remapping, so downward
    /// shifts lose resolution as the spectrum is squeezed into fewer bins; at
    /// small FFT sizes the extremes are coarse rather than clean.
    ///
    /// @param semitones Pitch shift [-48, +48]. Clamped to range.
    void setPitchShift(float semitones) noexcept {
        pitchShiftSemitones_ = std::clamp(semitones, -48.0f, 48.0f);
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
    void applyPitchShift(const float* inputMags, float* outputMags) const noexcept {
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
    /// (FR-018 floor). The +6 dB audio-level ceiling is enforced in processBlock
    /// via output sample clamping to [-2.0, +2.0], matching the AdditiveOscillator
    /// pattern. This keeps spectral tilt calculations clean while preventing
    /// IFFT overflow in the output domain where it matters.
    void applySpectralTilt(float* magnitudes) const noexcept {
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

            // FR-018: Clamp magnitude floor to zero
            magnitudes[k] = std::max(magnitudes[k], 0.0f);
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
        Krate::DSP::FormantPreserver::applyFormantPreservation(
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
        if (partialMode_) {
            synthesizePartialSpectrum();
        } else {
            synthesizeBinSpectrum();
        }

        // Inverse FFT
        fft_.inverse(workingSpectrum_.data(), ifftBuffer_.data());

        // Apply Hann synthesis window and COLA normalization, then overlap-add
        for (size_t i = 0; i < fftSize_; ++i) {
            const float windowed = ifftBuffer_[i] * synthesisWindow_[i]
                                 * colaNormalization_;
            const size_t outIdx = (outputWriteIndex_ + i) & outputMask_;
            outputBuffer_[outIdx] += windowed;
        }

        // Advance write index by hop size
        outputWriteIndex_ = (outputWriteIndex_ + hopSize_) & outputMask_;
        samplesInBuffer_ += hopSize_;

        if (partialMode_) return;  // partial phases advance in synthesizePartialSpectrum()

        // Advance phase accumulators for next frame (FR-008, FR-014)
        for (size_t k = 0; k < numBins_; ++k) {
            phaseAccumulators_[k] += phaseIncrements_[k];
            phaseAccumulators_[k] = wrapPhaseFast(phaseAccumulators_[k]);
        }
    }

    /// @brief Keep the frozen spectral peaks as partials (R-148).
    ///
    /// Each local maximum within 60 dB of the loudest bin becomes a partial whose
    /// true frequency is its bin centre, with the time-domain amplitude and phase
    /// of a bin-centred sinusoid (the unwindowed FFT puts N/2 * amplitude there).
    void capturePartials() noexcept {
        partialCount_ = 0;
        const float threshold = maxFrozenMagnitude_ * 1e-3f;
        if (!(threshold > 0.0f)) return;
        const double toAmplitude = 2.0 / static_cast<double>(fftSize_);
        for (size_t k = 1; k + 1 < numBins_ && partialCount_ < kMaxPartials; ++k) {
            const float m = frozenMagnitudes_[k];
            if (m > threshold && m > frozenMagnitudes_[k - 1] && m >= frozenMagnitudes_[k + 1]) {
                auto& p = partials_[partialCount_++];
                p.bin = static_cast<double>(k);
                p.amplitude = static_cast<double>(m) * toAmplitude;
                p.phase = static_cast<double>(initialPhases_[k]);
                p.gain = 1.0f;
            }
        }
    }

    /// @brief Frame spectrum of the shifted partials (R-148).
    ///
    /// For each partial at angular frequency w = 2 pi bin ratio / N, writes the
    /// exact N-point DFT of its frame segment A cos(w n + phi), n = 0..N-1, so the
    /// inverse FFT returns that segment and the overlap-add continues it without
    /// a seam: phi advances by w * hop per frame. With D(d) = sum_n e^{j d n}
    /// = (1 - e^{j N d}) / (1 - e^{j d}), bin k holds
    /// A/2 (e^{j phi} D(w - 2 pi k / N) + e^{-j phi} D(-w - 2 pi k / N)).
    /// Partials at or above Nyquist are dropped.
    void synthesizePartialSpectrum() noexcept {
        Complex* spectrum = workingSpectrum_.data();
        std::fill_n(spectrum, numBins_, Complex{});
        if (partialCount_ == 0) return;

        constexpr double kTwoPiD = 2.0 * std::numbers::pi;
        constexpr double kPiD = std::numbers::pi;
        const double n = static_cast<double>(fftSize_);
        const double ratio = static_cast<double>(semitonesToRatio(pitchShiftSemitones_));
        const double binOmega = kTwoPiD / n;

        // Spectral tilt at each partial's shifted frequency (same law as
        // applySpectralTilt: gain_dB = tilt * log2(f / bin-1 frequency)).
        for (size_t i = 0; i < partialCount_; ++i) {
            auto& p = partials_[i];
            const double shiftedBin = p.bin * ratio;
            p.gain = (spectralTiltDbPerOctave_ != 0.0f && shiftedBin > 0.0)
                ? std::pow(10.0f, spectralTiltDbPerOctave_
                                  * static_cast<float>(std::log2(shiftedBin)) / 20.0f)
                : 1.0f;
        }

        // Formant shift: the same envelope ratio the bin path applies, read at
        // each partial's nearest bin of a spectrum holding only the partials.
        if (formantShiftSemitones_ != 0.0f) {
            std::fill(workingMagnitudes_.begin(), workingMagnitudes_.end(), 0.0f);
            for (size_t i = 0; i < partialCount_; ++i) {
                const auto& p = partials_[i];
                const auto dest = static_cast<size_t>(std::lround(p.bin * ratio));
                if (dest < numBins_) {
                    workingMagnitudes_[dest] += static_cast<float>(p.amplitude) * p.gain;
                }
            }
            float* before = ifftBuffer_.data();  // scratch: overwritten by the IFFT
            std::copy_n(workingMagnitudes_.data(), numBins_, before);
            applyFormantShift(workingMagnitudes_.data());
            for (size_t i = 0; i < partialCount_; ++i) {
                auto& p = partials_[i];
                const auto dest = static_cast<size_t>(std::lround(p.bin * ratio));
                if (dest < numBins_ && before[dest] > 0.0f) {
                    p.gain *= workingMagnitudes_[dest] / before[dest];
                }
            }
        }

        // e^{-j 2 pi / N}: steps the bin twiddle e^{-j 2 pi k / N} from k = 0.
        const double stepRe = std::cos(binOmega);
        const double stepIm = -std::sin(binOmega);
        const double hop = static_cast<double>(hopSize_);

        for (size_t i = 0; i < partialCount_; ++i) {
            auto& p = partials_[i];
            const double w = binOmega * p.bin * ratio;
            if (w > 0.0 && w < kPiD) {
                const double halfAmp = 0.5 * p.amplitude * static_cast<double>(p.gain);
                // Coefficients of the two exponentials: (A/2) e^{+-j phi}.
                const double cRe = halfAmp * std::cos(p.phase);
                const double cIm = halfAmp * std::sin(p.phase);
                // Numerators 1 - e^{+-j N w} (bin independent: e^{-j 2 pi k} = 1).
                const double nwRe = std::cos(n * w);
                const double nwIm = std::sin(n * w);
                const double zRe = std::cos(w);
                const double zIm = std::sin(w);

                double tRe = 1.0;  // e^{-j 2 pi k / N}
                double tIm = 0.0;
                for (size_t k = 0; k < numBins_; ++k) {
                    const double binAngle = binOmega * static_cast<double>(k);
                    // Positive-frequency term: d = w - 2 pi k / N, e^{j d} = z t.
                    double posRe = 0.0;
                    double posIm = 0.0;
                    dirichlet(w - binAngle, n, 1.0 - nwRe, -nwIm,
                              zRe * tRe - zIm * tIm, zRe * tIm + zIm * tRe, posRe, posIm);
                    // Negative-frequency term: d = -w - 2 pi k / N, e^{j d} = conj(z) t.
                    double negRe = 0.0;
                    double negIm = 0.0;
                    dirichlet(-w - binAngle, n, 1.0 - nwRe, nwIm,
                              zRe * tRe + zIm * tIm, zRe * tIm - zIm * tRe, negRe, negIm);

                    // c * pos + conj(c) * neg
                    const double re = (cRe * posRe - cIm * posIm) + (cRe * negRe + cIm * negIm);
                    const double im = (cRe * posIm + cIm * posRe) + (cRe * negIm - cIm * negRe);
                    spectrum[k].real += static_cast<float>(re);
                    spectrum[k].imag += static_cast<float>(im);

                    const double nextRe = tRe * stepRe - tIm * stepIm;
                    tIm = tRe * stepIm + tIm * stepRe;
                    tRe = nextRe;
                }
            }
            // Advance to the next frame's start even while silent above Nyquist,
            // so a glide back down stays continuous.
            p.phase += w * hop;
            p.phase -= kTwoPiD * std::floor(p.phase / kTwoPiD);
        }
    }

    /// @brief D(d) = sum_{n<N} e^{j d n} given its numerator 1 - e^{j N d} and
    ///        e^{j d}; the removable singularity at d = 0 (mod 2 pi) is N e^{j d (N-1)/2}.
    static void dirichlet(double d, double n, double numRe, double numIm,
                          double edRe, double edIm, double& outRe, double& outIm) noexcept {
        constexpr double kTwoPiD = 2.0 * std::numbers::pi;
        const double wrapped = d - kTwoPiD * std::round(d / kTwoPiD);
        if (std::abs(wrapped) < 1e-6) {
            const double a = 0.5 * wrapped * (n - 1.0);
            outRe = n * std::cos(a);
            outIm = n * std::sin(a);
            return;
        }
        const double denRe = 1.0 - edRe;
        const double denIm = -edIm;
        const double inv = 1.0 / (denRe * denRe + denIm * denIm);
        outRe = (numRe * denRe + numIm * denIm) * inv;
        outIm = (numIm * denRe - numRe * denIm) * inv;
    }

    /// @brief Default frame spectrum: bin-remapped frozen magnitudes with
    ///        bin-centre phase accumulators.
    void synthesizeBinSpectrum() noexcept {
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

        // Construct complex spectrum from magnitudes + accumulated phases.
        //
        // BULK, NOT A PER-BIN std::cos/std::sin PAIR. The scalar loop this
        // replaces ran two libm transcendentals per bin per synthesis frame -
        // 2050 of them at the default 2048-point FFT - and MEASURED as the
        // dominant term of the whole oscillator. reconstructCartesianBulk is the
        // SIMD (Google Highway) form of exactly this expression, and it is the
        // same routine SpectralBuffer itself uses to rebuild Cartesian data from
        // its polar cache (spectral_buffer.h:193-195), so there is no second
        // implementation of the maths.
        //
        // The old loop forced bins with magnitude < 1e-20 to exactly (0, 0);
        // the bulk form multiplies them by cos/sin like any other bin, which
        // differs by at most 1e-20 per bin - below the denormal floor that
        // FTZ/DAZ flushes on the audio thread, and ~140 dB under the +/-2.0
        // output clamp.
        //
        // data() syncs Cartesian from the polar cache (a no-op in steady state)
        // and then invalidates polar, which is exactly right: the next writer of
        // this buffer is the line below.
        reconstructCartesianBulk(workingMagnitudes_.data(), phaseAccumulators_.data(),
                                 numBins_,
                                 reinterpret_cast<float*>(workingSpectrum_.data()));
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
    float maxFrozenMagnitude_ = 0.0f;  // Cached for FR-018 clamping
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
    size_t outputMask_ = 0;         ///< outputBuffer_.size() - 1 (a power of two)
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
    static constexpr size_t kMaxPartials = 16;

    // Partial resynthesis (setPartialResynthesis, R-148)
    struct FrozenPartial {
        double bin = 0.0;        ///< true frequency in bins (the peak bin's centre)
        double amplitude = 0.0;  ///< time-domain amplitude
        double phase = 0.0;      ///< phase at the start of the next frame
        float gain = 1.0f;       ///< tilt * formant gain of the current frame
    };
    std::array<FrozenPartial, kMaxPartials> partials_{};
    size_t partialCount_ = 0;
    bool partialMode_ = false;
};

} // namespace Krate::DSP

