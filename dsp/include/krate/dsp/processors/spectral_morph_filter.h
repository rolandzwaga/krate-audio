// ==============================================================================
// Layer 2: DSP Processor - Spectral Morph Filter
// ==============================================================================
// Morphs between two audio signals by interpolating their magnitude spectra
// while preserving phase from a selectable source.
//
// Features:
// - Dual-input spectral morphing (FR-002)
// - Single-input snapshot mode (FR-003)
// - Phase source selection: A, B, or Blend (FR-005)
// - Spectral shift via bin rotation (FR-007)
// - Spectral tilt with 1 kHz pivot (FR-008)
// - COLA-compliant overlap-add synthesis (FR-012)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept process, allocation in prepare())
// - Principle III: Modern C++ (C++20, RAII)
// - Principle IX: Layer 2 (depends on Layer 0-1 only)
// - Principle X: DSP Constraints (COLA windows, proper overlap)
// - Principle XII: Test-First Development
//
// Reference: specs/080-spectral-morph-filter/spec.md
// ==============================================================================

#pragma once

// Layer 0: Core
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/window_functions.h>

// Layer 1: Primitives
#include <krate/dsp/primitives/fft.h>
#include <krate/dsp/primitives/stft.h>
#include <krate/dsp/primitives/spectral_buffer.h>
#include <krate/dsp/primitives/smoother.h>

// Standard library
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Krate {
namespace DSP {

// =============================================================================
// PhaseSource Enumeration
// =============================================================================

/// @brief Phase source selection for spectral morphing
/// @note FR-005: System MUST provide setPhaseSource() with these options
enum class PhaseSource : uint8_t {
    A,      ///< Use phase from source A exclusively
    B,      ///< Use phase from source B exclusively
    Blend   ///< Interpolate via complex vector lerp (real/imag interpolation)
};

// =============================================================================
// SpectralMorphFilter Class
// =============================================================================

/// @brief Spectral Morph Filter - Layer 2 Processor
/// @note Morphs between two audio signals by interpolating magnitude spectra
///       while preserving phase from a selectable source.
class SpectralMorphFilter {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// FR-001: Supported FFT sizes
    static constexpr std::size_t kMinFFTSize = 256;
    static constexpr std::size_t kMaxFFTSize = 4096;
    static constexpr std::size_t kDefaultFFTSize = 2048;

    /// FR-004: Morph amount range
    static constexpr float kMinMorphAmount = 0.0f;
    static constexpr float kMaxMorphAmount = 1.0f;

    /// FR-007: Spectral shift range (semitones)
    static constexpr float kMinSpectralShift = -24.0f;
    static constexpr float kMaxSpectralShift = +24.0f;

    /// FR-008: Spectral tilt range (dB/octave) and pivot
    static constexpr float kMinSpectralTilt = -12.0f;
    static constexpr float kMaxSpectralTilt = +12.0f;
    static constexpr float kTiltPivotHz = 1000.0f;

    /// FR-006: Snapshot averaging
    static constexpr std::size_t kDefaultSnapshotFrames = 4;

    /// Smoothing time constant (ms)
    static constexpr float kSmoothingTimeMs = 50.0f;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    SpectralMorphFilter() noexcept = default;
    ~SpectralMorphFilter() noexcept = default;

    // Non-copyable, movable
    SpectralMorphFilter(const SpectralMorphFilter&) = delete;
    SpectralMorphFilter& operator=(const SpectralMorphFilter&) = delete;
    SpectralMorphFilter(SpectralMorphFilter&&) noexcept = default;
    SpectralMorphFilter& operator=(SpectralMorphFilter&&) noexcept = default;

    /// @brief Prepare for processing
    /// @param sampleRate Sample rate in Hz
    /// @param fftSize FFT size (power of 2, 256-4096)
    /// @pre fftSize is power of 2 within [kMinFFTSize, kMaxFFTSize]
    /// @note NOT real-time safe (allocates memory)
    /// @note FR-014
    void prepare(double sampleRate, std::size_t fftSize = kDefaultFFTSize) noexcept {
        // Clamp FFT size to valid range
        if (fftSize < kMinFFTSize) fftSize = kMinFFTSize;
        if (fftSize > kMaxFFTSize) fftSize = kMaxFFTSize;

        sampleRate_ = sampleRate;
        fftSize_ = fftSize;
        hopSize_ = fftSize / 2;  // 50% overlap for COLA with Hann

        // Prepare STFT analyzers (FR-009: reuse existing STFT)
        stftA_.prepare(fftSize, hopSize_, WindowType::Hann);
        stftB_.prepare(fftSize, hopSize_, WindowType::Hann);

        // Prepare overlap-add synthesizer
        overlapAdd_.prepare(fftSize, hopSize_, WindowType::Hann);

        // Prepare spectral buffers (FR-010: reuse existing SpectralBuffer)
        spectrumA_.prepare(fftSize);
        spectrumB_.prepare(fftSize);
        outputSpectrum_.prepare(fftSize);

        // Snapshot buffers
        snapshotSpectrum_.prepare(fftSize);
        snapshotAccumulator_.prepare(fftSize);

        // Temp buffers for spectral shift
        const std::size_t numBins = fftSize / 2 + 1;
        shiftedMagnitudes_.resize(numBins, 0.0f);
        shiftedPhases_.resize(numBins, 0.0f);

        // Configure smoothers (FR-018: smooth parameter changes)
        // Note: Smoothers are called once per frame, not once per sample,
        // so we configure them with the frame rate (sampleRate / hopSize)
        const float frameRate = static_cast<float>(sampleRate) / static_cast<float>(hopSize_);
        morphSmoother_.configure(kSmoothingTimeMs, frameRate);
        morphSmoother_.snapTo(morphAmount_);
        tiltSmoother_.configure(kSmoothingTimeMs, frameRate);
        tiltSmoother_.snapTo(spectralTilt_);

        // Clear snapshot state
        hasSnapshot_ = false;
        captureRequested_ = false;
        snapshotFramesAccumulated_ = 0;

        // Single-sample processing buffer for process(float)
        singleSampleInputBuffer_.resize(fftSize * 2, 0.0f);
        singleSampleOutputBuffer_.resize(fftSize * 2, 0.0f);
        singleSampleWritePos_ = 0;
        singleSampleReadPos_ = 0;

        // Zero buffer for nullptr input handling (pre-allocated for real-time safety)
        zeroBuffer_.resize(fftSize * 4, 0.0f);

        prepared_ = true;
    }

    /// @brief Reset all internal state buffers
    /// @note Real-time safe
    /// @note FR-013
    void reset() noexcept {
        if (!prepared_) return;

        stftA_.reset();
        stftB_.reset();
        overlapAdd_.reset();

        spectrumA_.reset();
        spectrumB_.reset();
        outputSpectrum_.reset();

        snapshotAccumulator_.reset();
        snapshotFramesAccumulated_ = 0;
        captureRequested_ = false;
        // Note: hasSnapshot_ and snapshotSpectrum_ preserved intentionally

        morphSmoother_.reset();
        morphSmoother_.snapTo(morphAmount_);
        tiltSmoother_.reset();
        tiltSmoother_.snapTo(spectralTilt_);

        std::fill(singleSampleInputBuffer_.begin(), singleSampleInputBuffer_.end(), 0.0f);
        std::fill(singleSampleOutputBuffer_.begin(), singleSampleOutputBuffer_.end(), 0.0f);
        singleSampleWritePos_ = 0;
        singleSampleReadPos_ = 0;
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process stereo block with dual inputs (cross-synthesis)
    /// @param inputA First input source buffer (nullptr treated as zeros)
    /// @param inputB Second input source buffer (nullptr treated as zeros)
    /// @param output Output buffer (must be at least numSamples)
    /// @param numSamples Number of samples to process
    /// @pre prepare() has been called
    /// @note Real-time safe, noexcept
    /// @note FR-002, FR-016
    void processBlock(const float* inputA, const float* inputB,
                      float* output, std::size_t numSamples) noexcept {
        if (!prepared_) {
            // Zero output if not prepared
            if (output != nullptr) {
                std::fill(output, output + numSamples, 0.0f);
            }
            return;
        }

        // Determine effective input pointers
        const float* effectiveA = (inputA != nullptr) ? inputA : zeroBuffer_.data();
        const float* effectiveB = (inputB != nullptr) ? inputB : zeroBuffer_.data();

        // Limit chunk size if using zero buffer
        const bool usingZeroBufferA = (inputA == nullptr);
        const bool usingZeroBufferB = (inputB == nullptr);
        const std::size_t maxChunk = (usingZeroBufferA || usingZeroBufferB) ? zeroBuffer_.size() : numSamples;

        // Track output position
        std::size_t outputWritten = 0;

        // Process samples in chunks
        std::size_t processed = 0;
        while (processed < numSamples) {
            const std::size_t chunkSize = std::min(numSamples - processed, maxChunk);

            // Get pointers for this chunk
            const float* chunkA = usingZeroBufferA ? zeroBuffer_.data() : effectiveA + processed;
            const float* chunkB = usingZeroBufferB ? zeroBuffer_.data() : effectiveB + processed;

            // Check for NaN/Inf in inputs (FR-015)
            bool hasInvalidInput = false;
            for (std::size_t i = 0; i < chunkSize; ++i) {
                if (!std::isfinite(chunkA[i]) || !std::isfinite(chunkB[i])) {
                    hasInvalidInput = true;
                    break;
                }
            }

            if (hasInvalidInput) {
                // Reset state and output silence for remaining samples
                reset();
                if (output != nullptr) {
                    std::fill(output + processed, output + numSamples, 0.0f);
                }
                return;
            }

            // Push samples into STFT analyzers
            stftA_.pushSamples(chunkA, chunkSize);
            stftB_.pushSamples(chunkB, chunkSize);

            // Process spectral frames when ready
            // IMPORTANT: Pull output immediately after each synthesize to avoid
            // overflow in the OverlapAdd buffer
            while (stftA_.canAnalyze() && stftB_.canAnalyze()) {
                // Analyze both inputs
                stftA_.analyze(spectrumA_);
                stftB_.analyze(spectrumB_);

                // Process the spectral frame
                processSpectralFrame();

                // Synthesize output
                overlapAdd_.synthesize(outputSpectrum_);

                // Pull hopSize samples immediately if available
                // This prevents buffer overflow and maintains correct overlap-add
                while (overlapAdd_.samplesAvailable() >= hopSize_ && outputWritten < numSamples) {
                    const std::size_t toPull = std::min(hopSize_, numSamples - outputWritten);
                    if (output != nullptr) {
                        overlapAdd_.pullSamples(output + outputWritten, toPull);
                    } else {
                        // Discard samples if no output buffer
                        std::vector<float> discard(toPull);
                        overlapAdd_.pullSamples(discard.data(), toPull);
                    }
                    outputWritten += toPull;
                }
            }

            processed += chunkSize;
        }

        // Fill remaining output with zeros if needed (latency warmup period)
        if (output != nullptr && outputWritten < numSamples) {
            std::fill(output + outputWritten, output + numSamples, 0.0f);
        }
    }

    /// @brief Process single sample (snapshot morphing mode)
    /// @param input Input sample
    /// @return Processed output sample
    /// @pre prepare() has been called
    /// @note Real-time safe, noexcept
    /// @note If no snapshot captured, returns input unchanged (passthrough)
    /// @note FR-003, FR-017
    [[nodiscard]] float process(float input) noexcept {
        if (!prepared_) {
            return 0.0f;
        }

        // Check for NaN/Inf
        if (!std::isfinite(input)) {
            reset();
            return 0.0f;
        }

        // Push sample to input buffer
        singleSampleInputBuffer_[singleSampleWritePos_] = input;
        singleSampleWritePos_ = (singleSampleWritePos_ + 1) % singleSampleInputBuffer_.size();

        // Push to STFT A for analysis
        stftA_.pushSamples(&input, 1);

        // Check if we can analyze
        if (stftA_.canAnalyze()) {
            stftA_.analyze(spectrumA_);

            // Handle snapshot capture
            if (captureRequested_) {
                accumulateSnapshotFrame(spectrumA_);
            }

            // Process frame
            if (hasSnapshot_) {
                // Morph between live input and snapshot
                processSpectralFrameWithSnapshot();
            } else {
                // No snapshot - passthrough (copy spectrum A to output)
                copySpectrum(spectrumA_, outputSpectrum_);
            }

            overlapAdd_.synthesize(outputSpectrum_);
        }

        // Pull output sample if available
        if (overlapAdd_.samplesAvailable() > 0) {
            float result = 0.0f;
            overlapAdd_.pullSamples(&result, 1);
            return result;
        }

        return 0.0f;
    }

    // =========================================================================
    // Snapshot
    // =========================================================================

    /// @brief Capture spectral snapshot from current input
    /// @note Averages NEXT N frames for smoother spectral fingerprint
    /// @note Replaces any existing snapshot
    /// @note FR-006
    void captureSnapshot() noexcept {
        if (!prepared_) return;

        // Reset accumulator
        snapshotAccumulator_.reset();
        snapshotFramesAccumulated_ = 0;
        captureRequested_ = true;
        hasSnapshot_ = false;  // Clear existing until new capture completes
    }

    /// @brief Set number of frames to average for snapshot
    /// @param frames Number of frames (typically 2-8)
    /// @note FR-006: Default 4 frames
    void setSnapshotFrameCount(std::size_t frames) noexcept {
        if (frames < 1) frames = 1;
        if (frames > 16) frames = 16;
        snapshotFrameCount_ = frames;
    }

    // =========================================================================
    // Parameters
    // =========================================================================

    /// @brief Set morph amount between sources
    /// @param amount 0.0 = source A only, 1.0 = source B only
    /// @note Smoothed internally to prevent clicks (FR-018)
    /// @note FR-004
    void setMorphAmount(float amount) noexcept {
        morphAmount_ = std::clamp(amount, kMinMorphAmount, kMaxMorphAmount);
        morphSmoother_.setTarget(morphAmount_);
    }

    /// @brief Set phase source for output
    /// @param source Phase source selection (A, B, or Blend)
    /// @note Blend uses complex vector interpolation
    /// @note FR-005
    void setPhaseSource(PhaseSource source) noexcept {
        phaseSource_ = source;
    }

    /// @brief Set spectral pitch shift
    /// @param semitones Shift amount (-24 to +24 semitones)
    /// @note Uses nearest-neighbor bin rounding
    /// @note Bins beyond Nyquist are zeroed
    /// @note FR-007
    void setSpectralShift(float semitones) noexcept {
        spectralShift_ = std::clamp(semitones, kMinSpectralShift, kMaxSpectralShift);
    }

    /// @brief Set spectral tilt (brightness control)
    /// @param dBPerOctave Tilt amount (-12 to +12 dB/octave)
    /// @note Pivot point at 1 kHz
    /// @note Smoothed internally to prevent clicks (FR-018)
    /// @note FR-008
    void setSpectralTilt(float dBPerOctave) noexcept {
        spectralTilt_ = std::clamp(dBPerOctave, kMinSpectralTilt, kMaxSpectralTilt);
        tiltSmoother_.setTarget(spectralTilt_);
    }

    // =========================================================================
    // Query
    // =========================================================================

    /// @brief Get processing latency in samples
    /// @return Latency equal to FFT size
    /// @note FR-020
    [[nodiscard]] std::size_t getLatencySamples() const noexcept {
        return fftSize_;
    }

    /// @brief Get current FFT size
    [[nodiscard]] std::size_t getFftSize() const noexcept {
        return fftSize_;
    }

    /// @brief Get current morph amount
    [[nodiscard]] float getMorphAmount() const noexcept {
        return morphAmount_;
    }

    /// @brief Get current phase source
    [[nodiscard]] PhaseSource getPhaseSource() const noexcept {
        return phaseSource_;
    }

    /// @brief Get current spectral shift
    [[nodiscard]] float getSpectralShift() const noexcept {
        return spectralShift_;
    }

    /// @brief Get current spectral tilt
    [[nodiscard]] float getSpectralTilt() const noexcept {
        return spectralTilt_;
    }

    /// @brief Check if snapshot has been captured
    [[nodiscard]] bool hasSnapshot() const noexcept {
        return hasSnapshot_;
    }

    /// @brief Check if processor is prepared
    [[nodiscard]] bool isPrepared() const noexcept {
        return prepared_;
    }

private:
    // =========================================================================
    // Internal Processing Methods
    // =========================================================================

    /// @brief Process a single spectral frame (dual-input mode)
    void processSpectralFrame() noexcept {
        // Get smoothed parameters
        const float morph = morphSmoother_.process();
        const float tilt = tiltSmoother_.process();

        // Step 1: Magnitude interpolation (FR-004)
        applyMagnitudeInterpolation(spectrumA_, spectrumB_, outputSpectrum_, morph);

        // Step 2: Phase selection (FR-005)
        applyPhaseSelection(spectrumA_, spectrumB_, outputSpectrum_, morph, phaseSource_);

        // Step 3: Spectral shift (FR-007)
        if (std::abs(spectralShift_) > 0.001f) {
            applySpectralShift(outputSpectrum_, spectralShift_);
        }

        // Step 4: Spectral tilt (FR-008)
        if (std::abs(tilt) > 0.001f) {
            applySpectralTilt(outputSpectrum_, tilt);
        }
    }

    /// @brief Process a single spectral frame (snapshot mode)
    void processSpectralFrameWithSnapshot() noexcept {
        const float morph = morphSmoother_.process();
        const float tilt = tiltSmoother_.process();

        // Morph between live spectrum (A) and snapshot
        applyMagnitudeInterpolation(spectrumA_, snapshotSpectrum_, outputSpectrum_, morph);

        // Phase from live input (A) by default in snapshot mode
        applyPhaseSelection(spectrumA_, snapshotSpectrum_, outputSpectrum_, morph, phaseSource_);

        // Apply spectral effects
        if (std::abs(spectralShift_) > 0.001f) {
            applySpectralShift(outputSpectrum_, spectralShift_);
        }

        if (std::abs(tilt) > 0.001f) {
            applySpectralTilt(outputSpectrum_, tilt);
        }
    }

    /// @brief Accumulate a frame for snapshot averaging
    void accumulateSnapshotFrame(const SpectralBuffer& spectrum) noexcept {
        const std::size_t numBins = spectrum.numBins();

        // Accumulate magnitudes
        for (std::size_t bin = 0; bin < numBins; ++bin) {
            const float currentMag = snapshotAccumulator_.getMagnitude(bin);
            const float newMag = spectrum.getMagnitude(bin);
            snapshotAccumulator_.setMagnitude(bin, currentMag + newMag);
        }

        // Store phase from last frame
        for (std::size_t bin = 0; bin < numBins; ++bin) {
            snapshotAccumulator_.setPhase(bin, spectrum.getPhase(bin));
        }

        ++snapshotFramesAccumulated_;

        // Check if capture complete
        if (snapshotFramesAccumulated_ >= snapshotFrameCount_) {
            finalizeSnapshot();
        }
    }

    /// @brief Finalize snapshot by averaging accumulated frames
    void finalizeSnapshot() noexcept {
        const std::size_t numBins = snapshotAccumulator_.numBins();
        const float invFrames = 1.0f / static_cast<float>(snapshotFrameCount_);

        // Average magnitudes and copy phase
        for (std::size_t bin = 0; bin < numBins; ++bin) {
            const float avgMag = snapshotAccumulator_.getMagnitude(bin) * invFrames;
            const float phase = snapshotAccumulator_.getPhase(bin);
            snapshotSpectrum_.setMagnitude(bin, avgMag);
            snapshotSpectrum_.setPhase(bin, phase);
        }

        hasSnapshot_ = true;
        captureRequested_ = false;
    }

    /// @brief Apply magnitude interpolation between two spectra
    void applyMagnitudeInterpolation(
        const SpectralBuffer& specA,
        const SpectralBuffer& specB,
        SpectralBuffer& output,
        float morphAmount
    ) noexcept {
        const std::size_t numBins = output.numBins();
        const float invMorph = 1.0f - morphAmount;

        for (std::size_t bin = 0; bin < numBins; ++bin) {
            const float magA = specA.getMagnitude(bin);
            const float magB = specB.getMagnitude(bin);
            const float blendedMag = magA * invMorph + magB * morphAmount;
            output.setMagnitude(bin, blendedMag);
        }
    }

    /// @brief Apply phase selection
    void applyPhaseSelection(
        const SpectralBuffer& specA,
        const SpectralBuffer& specB,
        SpectralBuffer& output,
        float morphAmount,
        PhaseSource source
    ) noexcept {
        const std::size_t numBins = output.numBins();

        switch (source) {
            case PhaseSource::A:
                // Use phase from source A exclusively
                for (std::size_t bin = 0; bin < numBins; ++bin) {
                    output.setPhase(bin, specA.getPhase(bin));
                }
                break;

            case PhaseSource::B:
                // Use phase from source B exclusively
                for (std::size_t bin = 0; bin < numBins; ++bin) {
                    output.setPhase(bin, specB.getPhase(bin));
                }
                break;

            case PhaseSource::Blend:
                // Complex vector interpolation (FR-005)
                // Interpolate real and imaginary components separately
                {
                    const float invMorph = 1.0f - morphAmount;
                    for (std::size_t bin = 0; bin < numBins; ++bin) {
                        // Get current output magnitude (already interpolated)
                        const float mag = output.getMagnitude(bin);

                        // Get complex values from both sources
                        const float realA = specA.getReal(bin);
                        const float imagA = specA.getImag(bin);
                        const float realB = specB.getReal(bin);
                        const float imagB = specB.getImag(bin);

                        // Interpolate complex values
                        const float blendedReal = realA * invMorph + realB * morphAmount;
                        const float blendedImag = imagA * invMorph + imagB * morphAmount;

                        // Extract phase from blended complex
                        const float blendedPhase = std::atan2(blendedImag, blendedReal);

                        // Set phase (magnitude already set)
                        output.setPhase(bin, blendedPhase);
                    }
                }
                break;
        }
    }

    /// @brief Apply spectral shift via bin rotation
    void applySpectralShift(SpectralBuffer& spectrum, float semitones) noexcept {
        const std::size_t numBins = spectrum.numBins();

        // Convert semitones to frequency ratio: ratio = 2^(semitones/12)
        const float ratio = std::pow(2.0f, semitones / 12.0f);

        // Clear temp buffers
        std::fill(shiftedMagnitudes_.begin(), shiftedMagnitudes_.end(), 0.0f);
        std::fill(shiftedPhases_.begin(), shiftedPhases_.end(), 0.0f);

        // For each output bin, find the source bin
        // Output bin k corresponds to frequency f_k
        // Source frequency = f_k / ratio
        // Source bin = k / ratio (nearest-neighbor rounding)
        for (std::size_t outBin = 0; outBin < numBins; ++outBin) {
            // Calculate source bin (nearest-neighbor rounding)
            const float srcBinFloat = static_cast<float>(outBin) / ratio;
            const std::size_t srcBin = static_cast<std::size_t>(std::round(srcBinFloat));

            // If source bin is valid, copy magnitude and phase
            if (srcBin < numBins) {
                shiftedMagnitudes_[outBin] = spectrum.getMagnitude(srcBin);
                shiftedPhases_[outBin] = spectrum.getPhase(srcBin);
            }
            // else: bin stays at zero (beyond Nyquist)
        }

        // Copy back to spectrum
        for (std::size_t bin = 0; bin < numBins; ++bin) {
            spectrum.setMagnitude(bin, shiftedMagnitudes_[bin]);
            spectrum.setPhase(bin, shiftedPhases_[bin]);
        }
    }

    /// @brief Apply spectral tilt with 1 kHz pivot
    void applySpectralTilt(SpectralBuffer& spectrum, float dBPerOctave) noexcept {
        const std::size_t numBins = spectrum.numBins();
        const float binFreqStep = static_cast<float>(sampleRate_) / static_cast<float>(fftSize_);

        // Pivot at 1 kHz
        constexpr float pivotFreq = kTiltPivotHz;

        for (std::size_t bin = 1; bin < numBins; ++bin) {  // Skip DC bin
            const float binFreq = static_cast<float>(bin) * binFreqStep;

            // Calculate octave distance from pivot: octaves = log2(freq / pivot)
            const float octaves = std::log2(binFreq / pivotFreq);

            // Calculate gain: gain_dB = tilt * octaves
            const float gainDb = dBPerOctave * octaves;

            // Convert to linear: gain = 10^(gainDb/20)
            const float gainLinear = std::pow(10.0f, gainDb / 20.0f);

            // Apply gain to magnitude
            const float currentMag = spectrum.getMagnitude(bin);
            spectrum.setMagnitude(bin, currentMag * gainLinear);
        }
    }

    /// @brief Copy spectrum from source to destination
    void copySpectrum(const SpectralBuffer& src, SpectralBuffer& dst) noexcept {
        const std::size_t numBins = src.numBins();
        for (std::size_t bin = 0; bin < numBins; ++bin) {
            const float real = src.getReal(bin);
            const float imag = src.getImag(bin);
            dst.setCartesian(bin, real, imag);
        }
    }

    // =========================================================================
    // State
    // =========================================================================

    // Configuration
    double sampleRate_ = 44100.0;
    std::size_t fftSize_ = kDefaultFFTSize;
    std::size_t hopSize_ = kDefaultFFTSize / 2;
    bool prepared_ = false;

    // STFT components
    STFT stftA_;
    STFT stftB_;
    OverlapAdd overlapAdd_;

    // Spectral buffers
    SpectralBuffer spectrumA_;
    SpectralBuffer spectrumB_;
    SpectralBuffer outputSpectrum_;

    // Snapshot state
    SpectralBuffer snapshotSpectrum_;
    SpectralBuffer snapshotAccumulator_;
    std::size_t snapshotFrameCount_ = kDefaultSnapshotFrames;
    std::size_t snapshotFramesAccumulated_ = 0;
    bool hasSnapshot_ = false;
    bool captureRequested_ = false;

    // Parameters
    float morphAmount_ = 0.0f;
    float spectralShift_ = 0.0f;
    float spectralTilt_ = 0.0f;
    PhaseSource phaseSource_ = PhaseSource::A;

    // Parameter smoothing
    OnePoleSmoother morphSmoother_;
    OnePoleSmoother tiltSmoother_;

    // Temp buffers for spectral shift
    std::vector<float> shiftedMagnitudes_;
    std::vector<float> shiftedPhases_;

    // Single-sample processing buffers
    std::vector<float> singleSampleInputBuffer_;
    std::vector<float> singleSampleOutputBuffer_;
    std::size_t singleSampleWritePos_ = 0;
    std::size_t singleSampleReadPos_ = 0;

    // Zero buffer for nullptr input handling (pre-allocated)
    std::vector<float> zeroBuffer_;
};

} // namespace DSP
} // namespace Krate
