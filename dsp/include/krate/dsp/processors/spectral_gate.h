// ==============================================================================
// Layer 2: DSP Processor - Spectral Gate
// ==============================================================================
// Per-bin noise gate that passes frequency components above a magnitude threshold
// while creating spectral holes below threshold.
//
// Features:
// - Per-bin noise gating with configurable threshold (FR-001, FR-004)
// - Configurable FFT sizes: 256, 512, 1024, 2048, 4096 (FR-002)
// - COLA-compliant overlap-add synthesis (FR-003)
// - Expansion ratio from 1:1 (bypass) to 100:1 (hard gate) (FR-005)
// - Per-bin attack/release envelope tracking (FR-006, FR-007, FR-008)
// - Frequency range limiting (FR-009, FR-010)
// - Spectral smearing for reduced musical noise (FR-011, FR-012, FR-013)
// - Real-time safe processing (FR-018, FR-019, FR-020)
// - Click-free parameter changes (FR-021, FR-022)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept process, allocation in prepare())
// - Principle III: Modern C++ (C++20, RAII)
// - Principle IX: Layer 2 (depends on Layer 0-1 only)
// - Principle X: DSP Constraints (COLA windows, proper overlap)
// - Principle XII: Test-First Development
//
// Reference: specs/081-spectral-gate/spec.md
// ==============================================================================

#pragma once

// Layer 0: Core
#include <krate/dsp/core/db_utils.h>
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
#include <vector>

namespace Krate {
namespace DSP {

/// @brief Layer 2 DSP Processor - Per-bin spectral noise gate
///
/// Performs noise gating in the frequency domain by independently gating
/// each frequency bin based on its magnitude relative to a threshold.
/// Features attack/release envelopes per bin, expansion ratio control,
/// frequency range limiting, and spectral smearing for reduced artifacts.
///
/// @par Features
/// - Per-bin noise gating with configurable threshold (FR-001, FR-004)
/// - Configurable FFT sizes: 256, 512, 1024, 2048, 4096 (FR-002)
/// - COLA-compliant overlap-add synthesis (FR-003)
/// - Expansion ratio from 1:1 (bypass) to 100:1 (hard gate) (FR-005)
/// - Per-bin attack/release envelope tracking (FR-006, FR-007, FR-008)
/// - Frequency range limiting (FR-009, FR-010)
/// - Spectral smearing for reduced musical noise (FR-011, FR-012, FR-013)
/// - Real-time safe processing (FR-018, FR-019, FR-020)
/// - Click-free parameter changes (FR-021, FR-022)
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept process, allocation in prepare())
/// - Principle III: Modern C++ (C++20, RAII)
/// - Principle IX: Layer 2 (depends on Layer 0-1 only)
/// - Principle X: DSP Constraints (COLA windows, proper overlap)
/// - Principle XII: Test-First Development
///
/// @par Usage
/// @code
/// SpectralGate gate;
/// gate.prepare(44100.0, 1024);
/// gate.setThreshold(-40.0f);
/// gate.setRatio(100.0f);  // Hard gate
/// gate.setAttack(10.0f);
/// gate.setRelease(100.0f);
///
/// // In process callback
/// gate.processBlock(buffer, numSamples);
/// @endcode
class SpectralGate {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// FR-002: Supported FFT sizes
    static constexpr std::size_t kMinFFTSize = 256;
    static constexpr std::size_t kMaxFFTSize = 4096;
    static constexpr std::size_t kDefaultFFTSize = 1024;

    /// FR-004: Threshold range (dB)
    static constexpr float kMinThresholdDb = -96.0f;
    static constexpr float kMaxThresholdDb = 0.0f;
    static constexpr float kDefaultThresholdDb = -40.0f;

    /// FR-005: Ratio range (100:1 = practical infinity for hard gate)
    static constexpr float kMinRatio = 1.0f;
    static constexpr float kMaxRatio = 100.0f;
    static constexpr float kDefaultRatio = 100.0f;

    /// FR-006: Attack time range (ms)
    static constexpr float kMinAttackMs = 0.1f;
    static constexpr float kMaxAttackMs = 500.0f;
    static constexpr float kDefaultAttackMs = 10.0f;

    /// FR-007: Release time range (ms)
    static constexpr float kMinReleaseMs = 1.0f;
    static constexpr float kMaxReleaseMs = 5000.0f;
    static constexpr float kDefaultReleaseMs = 100.0f;

    /// FR-009: Frequency range bounds (Hz)
    static constexpr float kMinFrequencyHz = 20.0f;
    static constexpr float kMaxFrequencyHz = 20000.0f;

    /// FR-011: Smearing amount range
    static constexpr float kMinSmearAmount = 0.0f;
    static constexpr float kMaxSmearAmount = 1.0f;

    /// Parameter smoothing time constant
    static constexpr float kSmoothingTimeMs = 50.0f;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    SpectralGate() noexcept = default;
    ~SpectralGate() noexcept = default;

    // Non-copyable, movable
    SpectralGate(const SpectralGate&) = delete;
    SpectralGate& operator=(const SpectralGate&) = delete;
    SpectralGate(SpectralGate&&) noexcept = default;
    SpectralGate& operator=(SpectralGate&&) noexcept = default;

    /// @brief Prepare for processing (FR-014)
    /// @param sampleRate Sample rate in Hz
    /// @param fftSize FFT size (power of 2, 256-4096)
    /// @pre fftSize is power of 2 within [kMinFFTSize, kMaxFFTSize]
    /// @note NOT real-time safe (allocates memory)
    void prepare(double sampleRate, std::size_t fftSize = kDefaultFFTSize) noexcept {
        // Clamp FFT size to valid range
        if (fftSize < kMinFFTSize) fftSize = kMinFFTSize;
        if (fftSize > kMaxFFTSize) fftSize = kMaxFFTSize;

        // Round to nearest power of 2
        std::size_t pow2 = 1;
        while (pow2 < fftSize) pow2 <<= 1;
        if (pow2 > kMaxFFTSize) pow2 = kMaxFFTSize;
        fftSize = pow2;

        sampleRate_ = sampleRate;
        fftSize_ = fftSize;
        hopSize_ = fftSize / 2;  // 50% overlap for COLA with Hann
        numBins_ = fftSize / 2 + 1;

        // Calculate frame rate for envelope coefficients
        frameRate_ = static_cast<float>(sampleRate) / static_cast<float>(hopSize_);

        // Prepare STFT analyzer
        stft_.prepare(fftSize, hopSize_, WindowType::Hann);

        // Prepare overlap-add synthesizer
        overlapAdd_.prepare(fftSize, hopSize_, WindowType::Hann);

        // Prepare spectral buffers
        inputSpectrum_.prepare(fftSize);
        outputSpectrum_.prepare(fftSize);

        // Allocate per-bin state vectors
        binEnvelopes_.resize(numBins_, 0.0f);
        gateGains_.resize(numBins_, 1.0f);
        smearedGains_.resize(numBins_, 1.0f);

        // Configure parameter smoothers (process at frame rate)
        thresholdSmoother_.configure(kSmoothingTimeMs, frameRate_);
        thresholdSmoother_.snapTo(thresholdDb_);
        ratioSmoother_.configure(kSmoothingTimeMs, frameRate_);
        ratioSmoother_.snapTo(ratio_);

        // Update derived coefficients
        updateCoefficients();
        updateFrequencyRange();
        updateSmearKernel();

        // Allocate auxiliary buffers
        zeroBuffer_.resize(fftSize * 4, 0.0f);
        singleSampleInputBuffer_.resize(fftSize * 2, 0.0f);
        singleSampleOutputBuffer_.resize(fftSize * 2, 0.0f);
        singleSampleWritePos_ = 0;
        singleSampleReadPos_ = 0;

        prepared_ = true;
    }

    /// @brief Reset all internal state buffers (FR-015)
    /// @note Real-time safe
    void reset() noexcept {
        if (!prepared_) return;

        stft_.reset();
        overlapAdd_.reset();

        inputSpectrum_.reset();
        outputSpectrum_.reset();

        // Reset per-bin state
        std::fill(binEnvelopes_.begin(), binEnvelopes_.end(), 0.0f);
        std::fill(gateGains_.begin(), gateGains_.end(), 1.0f);
        std::fill(smearedGains_.begin(), smearedGains_.end(), 1.0f);

        // Reset parameter smoothers
        thresholdSmoother_.reset();
        thresholdSmoother_.snapTo(thresholdDb_);
        ratioSmoother_.reset();
        ratioSmoother_.snapTo(ratio_);

        // Reset single-sample buffers
        std::fill(singleSampleInputBuffer_.begin(), singleSampleInputBuffer_.end(), 0.0f);
        std::fill(singleSampleOutputBuffer_.begin(), singleSampleOutputBuffer_.end(), 0.0f);
        singleSampleWritePos_ = 0;
        singleSampleReadPos_ = 0;
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process a single sample (FR-016)
    /// @param input Input sample
    /// @return Processed output sample
    /// @pre prepare() has been called
    /// @note Real-time safe, noexcept (FR-019)
    [[nodiscard]] float process(float input) noexcept {
        if (!prepared_) {
            return 0.0f;
        }

        // Check for NaN/Inf using bit-level checks (works with -ffast-math)
        if (detail::isNaN(input) || detail::isInf(input)) {
            reset();
            return 0.0f;
        }

        // Push sample to STFT
        stft_.pushSamples(&input, 1);

        // Check if we can analyze
        if (stft_.canAnalyze()) {
            stft_.analyze(inputSpectrum_);
            processSpectralFrame();
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

    /// @brief Process a block of audio in-place (FR-017)
    /// @param buffer Audio buffer (modified in-place)
    /// @param numSamples Number of samples to process
    /// @pre prepare() has been called
    /// @note Real-time safe, noexcept (FR-019)
    void processBlock(float* buffer, std::size_t numSamples) noexcept {
        if (!prepared_) {
            if (buffer != nullptr) {
                std::fill(buffer, buffer + numSamples, 0.0f);
            }
            return;
        }

        // Handle nullptr input
        if (buffer == nullptr) {
            buffer = zeroBuffer_.data();
            numSamples = std::min(numSamples, zeroBuffer_.size());
        }

        // Handle zero samples
        if (numSamples == 0) {
            return;
        }

        // Check for NaN/Inf in input using bit-level checks (works with -ffast-math)
        bool hasInvalidInput = false;
        for (std::size_t i = 0; i < numSamples; ++i) {
            if (detail::isNaN(buffer[i]) || detail::isInf(buffer[i])) {
                hasInvalidInput = true;
                break;
            }
        }

        if (hasInvalidInput) {
            reset();
            std::fill(buffer, buffer + numSamples, 0.0f);
            return;
        }

        // Track output position
        std::size_t outputWritten = 0;

        // Push all samples to STFT
        stft_.pushSamples(buffer, numSamples);

        // Process spectral frames when ready
        while (stft_.canAnalyze()) {
            stft_.analyze(inputSpectrum_);
            processSpectralFrame();
            overlapAdd_.synthesize(outputSpectrum_);

            // Pull hopSize samples immediately if available
            while (overlapAdd_.samplesAvailable() >= hopSize_ && outputWritten < numSamples) {
                std::size_t toPull = std::min(hopSize_, numSamples - outputWritten);
                overlapAdd_.pullSamples(buffer + outputWritten, toPull);
                outputWritten += toPull;
            }
        }

        // Fill remaining output with zeros if needed (latency warmup period)
        if (outputWritten < numSamples) {
            std::fill(buffer + outputWritten, buffer + numSamples, 0.0f);
        }
    }

    // =========================================================================
    // Threshold and Ratio Parameters
    // =========================================================================

    /// @brief Set gate threshold (FR-004)
    /// @param dB Threshold in decibels [-96, 0]
    /// @note Smoothed internally to prevent clicks (FR-021)
    void setThreshold(float dB) noexcept {
        thresholdDb_ = std::clamp(dB, kMinThresholdDb, kMaxThresholdDb);
        thresholdSmoother_.setTarget(thresholdDb_);
    }

    /// @brief Get current threshold setting
    /// @return Threshold in decibels
    [[nodiscard]] float getThreshold() const noexcept {
        return thresholdDb_;
    }

    /// @brief Set expansion ratio (FR-005)
    /// @param ratio Expansion ratio [1.0, 100.0] (100.0 = hard gate)
    /// @note Smoothed internally to prevent clicks (FR-022)
    void setRatio(float ratio) noexcept {
        ratio_ = std::clamp(ratio, kMinRatio, kMaxRatio);
        ratioSmoother_.setTarget(ratio_);
    }

    /// @brief Get current ratio setting
    /// @return Expansion ratio
    [[nodiscard]] float getRatio() const noexcept {
        return ratio_;
    }

    // =========================================================================
    // Envelope Parameters
    // =========================================================================

    /// @brief Set per-bin attack time (FR-006)
    /// @param ms Attack time in milliseconds [0.1, 500]
    /// @note 10%-90% rise time measurement
    void setAttack(float ms) noexcept {
        attackMs_ = std::clamp(ms, kMinAttackMs, kMaxAttackMs);
        updateCoefficients();
    }

    /// @brief Get current attack time
    /// @return Attack time in milliseconds
    [[nodiscard]] float getAttack() const noexcept {
        return attackMs_;
    }

    /// @brief Set per-bin release time (FR-007)
    /// @param ms Release time in milliseconds [1, 5000]
    /// @note 90%-10% fall time measurement
    void setRelease(float ms) noexcept {
        releaseMs_ = std::clamp(ms, kMinReleaseMs, kMaxReleaseMs);
        updateCoefficients();
    }

    /// @brief Get current release time
    /// @return Release time in milliseconds
    [[nodiscard]] float getRelease() const noexcept {
        return releaseMs_;
    }

    // =========================================================================
    // Frequency Range Parameters
    // =========================================================================

    /// @brief Set frequency range for gating (FR-009)
    /// @param lowHz Lower frequency bound in Hz
    /// @param highHz Upper frequency bound in Hz
    /// @note Bins outside range pass through unaffected (FR-010)
    /// @note Boundaries rounded to nearest bin center
    void setFrequencyRange(float lowHz, float highHz) noexcept {
        // Swap if necessary
        if (lowHz > highHz) {
            std::swap(lowHz, highHz);
        }

        lowHz_ = std::clamp(lowHz, kMinFrequencyHz, kMaxFrequencyHz);
        highHz_ = std::clamp(highHz, kMinFrequencyHz, kMaxFrequencyHz);
        updateFrequencyRange();
    }

    /// @brief Get lower frequency bound
    /// @return Low frequency in Hz
    [[nodiscard]] float getLowFrequency() const noexcept {
        return lowHz_;
    }

    /// @brief Get upper frequency bound
    /// @return High frequency in Hz
    [[nodiscard]] float getHighFrequency() const noexcept {
        return highHz_;
    }

    // =========================================================================
    // Smearing Parameters
    // =========================================================================

    /// @brief Set spectral smearing amount (FR-011)
    /// @param amount Smearing [0, 1] (0 = off, 1 = maximum)
    /// @note 0 = independent per-bin processing (FR-012)
    /// @note 1 = maximum neighbor influence (FR-013)
    void setSmearing(float amount) noexcept {
        smearAmount_ = std::clamp(amount, kMinSmearAmount, kMaxSmearAmount);
        updateSmearKernel();
    }

    /// @brief Get current smearing amount
    /// @return Smearing amount [0, 1]
    [[nodiscard]] float getSmearing() const noexcept {
        return smearAmount_;
    }

    // =========================================================================
    // Query
    // =========================================================================

    /// @brief Get processing latency in samples
    /// @return Latency equal to FFT size (SC-003)
    [[nodiscard]] std::size_t getLatencySamples() const noexcept {
        return fftSize_;
    }

    /// @brief Get current FFT size
    [[nodiscard]] std::size_t getFftSize() const noexcept {
        return fftSize_;
    }

    /// @brief Get number of frequency bins
    [[nodiscard]] std::size_t getNumBins() const noexcept {
        return numBins_;
    }

    /// @brief Check if processor is prepared
    [[nodiscard]] bool isPrepared() const noexcept {
        return prepared_;
    }

private:
    // =========================================================================
    // Internal Processing Methods
    // =========================================================================

    /// @brief Process a single spectral frame (OPTIMIZED)
    /// Combines envelope update, gain computation, and gain application in merged loops
    /// for better cache locality. Uses linear-domain threshold comparison to avoid
    /// per-bin dB conversions.
    void processSpectralFrame() noexcept {
        // Get smoothed parameters
        const float thresholdDb = thresholdSmoother_.process();
        const float ratio = ratioSmoother_.process();

        // OPTIMIZATION 1: Convert threshold to linear domain once per frame
        // This avoids calling gainToDb() per bin (which uses log10)
        // Reference level: full scale sine with Hann window = fftSize/4
        const float referenceLevel = static_cast<float>(fftSize_) / 4.0f;
        const float thresholdLinear = referenceLevel * dbToGain(thresholdDb);

        // Precompute for expansion calculation
        const bool isBypass = (ratio <= 1.0f + 1e-6f);
        const float ratioMinus1 = ratio - 1.0f;

        // Minimum envelope for gain calculation (corresponds to -144 dB)
        const float minEnvelope = referenceLevel * dbToGain(kSilenceFloorDb);

        // OPTIMIZATION 3: Merged loop - envelope update + gain calculation in single pass
        for (std::size_t bin = 0; bin < numBins_; ++bin) {
            const float magnitude = inputSpectrum_.getMagnitude(bin);
            float& envelope = binEnvelopes_[bin];

            // Asymmetric one-pole filter: attack for rising, release for falling
            // Branchless version enables better compiler optimization
            const float coeff = (magnitude > envelope) ? attackCoeff_ : releaseCoeff_;
            envelope = envelope + coeff * (magnitude - envelope);
            envelope = detail::flushDenormal(envelope);

            // Check if bin is outside frequency range - pass through
            if (bin < lowBin_ || bin > highBin_) {
                gateGains_[bin] = 1.0f;
                continue;
            }

            // OPTIMIZATION 1 continued: Linear domain comparison (no log10 per bin)
            if (envelope >= thresholdLinear) {
                // Above threshold - unity gain
                gateGains_[bin] = 1.0f;
            } else if (isBypass) {
                // Ratio = 1 means bypass (no expansion)
                gateGains_[bin] = 1.0f;
            } else {
                // Below threshold - apply expansion in linear domain
                // Original formula: gain = dbToGain(-(thresholdDb - envelopeDb) * (ratio - 1))
                // Equivalent linear: gain = (envelope / thresholdLinear)^(ratio - 1)
                // This uses std::pow but avoids log10 + pow10 pair
                const float envClamped = std::max(envelope, minEnvelope);
                const float normalizedEnv = envClamped / thresholdLinear;
                const float gain = std::pow(normalizedEnv, ratioMinus1);
                gateGains_[bin] = detail::flushDenormal(gain);
            }
        }

        // Apply spectral smearing if enabled
        if (smearKernelSize_ > 1) {
            applySmearingOptimized();
        } else {
            // No smearing - copy gains directly
            std::copy(gateGains_.begin(), gateGains_.end(), smearedGains_.begin());
        }

        // Apply gains to spectrum (merged with output copy)
        applyGainsOptimized();
    }

    /// @brief Update per-bin envelopes from current magnitude spectrum
    /// @note Kept for compatibility but no longer used in optimized path
    void updateBinEnvelopes() noexcept {
        for (std::size_t bin = 0; bin < numBins_; ++bin) {
            const float magnitude = inputSpectrum_.getMagnitude(bin);
            float& envelope = binEnvelopes_[bin];

            const float coeff = (magnitude > envelope) ? attackCoeff_ : releaseCoeff_;
            envelope = envelope + coeff * (magnitude - envelope);
            envelope = detail::flushDenormal(envelope);
        }
    }

    /// @brief Compute gate gains from envelopes and threshold
    /// @note Kept for compatibility but no longer used in optimized path
    void computeGateGains() noexcept {
        const float threshold = thresholdSmoother_.getCurrentValue();
        const float ratio = ratioSmoother_.getCurrentValue();
        computeGateGainsInternal(threshold, ratio);
    }

    /// @brief Internal gate gain computation with explicit parameters
    /// @note Kept for compatibility but no longer used in optimized path
    void computeGateGainsInternal(float thresholdDb, float ratio) noexcept {
        const float referenceLevel = static_cast<float>(fftSize_) / 4.0f;
        const float thresholdLinear = referenceLevel * dbToGain(thresholdDb);
        const bool isBypass = (ratio <= 1.0f + 1e-6f);
        const float ratioMinus1 = ratio - 1.0f;
        const float minEnvelope = referenceLevel * dbToGain(kSilenceFloorDb);

        for (std::size_t bin = 0; bin < numBins_; ++bin) {
            if (bin < lowBin_ || bin > highBin_) {
                gateGains_[bin] = 1.0f;
                continue;
            }

            const float envelope = binEnvelopes_[bin];

            if (envelope >= thresholdLinear) {
                gateGains_[bin] = 1.0f;
            } else if (isBypass) {
                gateGains_[bin] = 1.0f;
            } else {
                const float envClamped = std::max(envelope, minEnvelope);
                const float normalizedEnv = envClamped / thresholdLinear;
                const float gain = std::pow(normalizedEnv, ratioMinus1);
                gateGains_[bin] = detail::flushDenormal(gain);
            }
        }
    }

    /// @brief Apply smearing to gate gains (OPTIMIZED - O(n) sliding window)
    /// Uses running sum instead of nested loops for O(n) instead of O(n*k)
    void applySmearingOptimized() noexcept {
        const std::size_t halfKernel = smearKernelSize_ / 2;

        // Initialize running sum for first window
        float runningSum = 0.0f;
        std::size_t windowSize = 0;

        // Build initial window (from bin 0 to bin halfKernel)
        for (std::size_t i = 0; i <= halfKernel && i < numBins_; ++i) {
            runningSum += gateGains_[i];
            ++windowSize;
        }

        // Process each bin with sliding window
        for (std::size_t bin = 0; bin < numBins_; ++bin) {
            // Add right edge of window (if within bounds)
            if (bin > 0 && bin + halfKernel < numBins_) {
                runningSum += gateGains_[bin + halfKernel];
                ++windowSize;
            }

            // Remove left edge of window (if it was included)
            if (bin > halfKernel) {
                runningSum -= gateGains_[bin - halfKernel - 1];
                --windowSize;
            }

            // Compute average
            smearedGains_[bin] = (windowSize > 0)
                ? detail::flushDenormal(runningSum / static_cast<float>(windowSize))
                : gateGains_[bin];
        }
    }

    /// @brief Apply smearing to gate gains (original O(n*k) version for reference)
    void applySmearing() noexcept {
        applySmearingOptimized();
    }

    /// @brief Apply gate gains to spectrum (OPTIMIZED - merged magnitude read/write)
    void applyGainsOptimized() noexcept {
        for (std::size_t bin = 0; bin < numBins_; ++bin) {
            const float gain = smearedGains_[bin];

            // Read magnitude, apply gain, write back
            // Phase is preserved (not modified)
            const float magnitude = inputSpectrum_.getMagnitude(bin);
            const float newMagnitude = magnitude * gain;

            // Copy phase directly and set new magnitude
            outputSpectrum_.setMagnitude(bin, newMagnitude);
            outputSpectrum_.setPhase(bin, inputSpectrum_.getPhase(bin));
        }
    }

    /// @brief Apply gate gains to spectrum (original version)
    void applyGains() noexcept {
        applyGainsOptimized();
    }

    /// @brief Update attack/release coefficients
    void updateCoefficients() noexcept {
        if (frameRate_ <= 0.0f) return;

        // Convert ms to frame-rate coefficients
        // Using 10%-90% rise time semantics: tau = time / 2.197
        // coeff = 1 - exp(-1 / (tau * frameRate))

        // Attack coefficient
        const float attackTau = (attackMs_ * 0.001f * frameRate_) / 2.197f;
        if (attackTau > 0.0f) {
            attackCoeff_ = 1.0f - std::exp(-1.0f / attackTau);
        } else {
            attackCoeff_ = 1.0f;  // Instant attack
        }

        // Release coefficient
        const float releaseTau = (releaseMs_ * 0.001f * frameRate_) / 2.197f;
        if (releaseTau > 0.0f) {
            releaseCoeff_ = 1.0f - std::exp(-1.0f / releaseTau);
        } else {
            releaseCoeff_ = 1.0f;  // Instant release
        }
    }

    /// @brief Update frequency range bin indices
    void updateFrequencyRange() noexcept {
        lowBin_ = hzToBin(lowHz_);
        highBin_ = hzToBin(highHz_);

        // Ensure valid range
        if (highBin_ >= numBins_) highBin_ = numBins_ - 1;
        if (lowBin_ > highBin_) lowBin_ = highBin_;
    }

    /// @brief Update smearing kernel size
    void updateSmearKernel() noexcept {
        // Map smearAmount [0,1] to kernel size [1, fftSize/64]
        const std::size_t maxKernel = std::max(fftSize_ / 64, std::size_t(1));
        smearKernelSize_ = 1 + static_cast<std::size_t>(smearAmount_ * static_cast<float>(maxKernel - 1));

        // Ensure odd kernel for symmetric averaging
        if (smearKernelSize_ % 2 == 0 && smearKernelSize_ > 1) {
            smearKernelSize_ += 1;
        }
    }

    /// @brief Convert Hz to bin index (round to nearest)
    [[nodiscard]] std::size_t hzToBin(float hz) const noexcept {
        if (sampleRate_ <= 0.0 || fftSize_ == 0) return 0;
        const float bin = hz * static_cast<float>(fftSize_) / static_cast<float>(sampleRate_);
        return static_cast<std::size_t>(std::round(std::max(0.0f, bin)));
    }

    // =========================================================================
    // State
    // =========================================================================

    // Configuration
    double sampleRate_ = 44100.0;
    std::size_t fftSize_ = kDefaultFFTSize;
    std::size_t hopSize_ = kDefaultFFTSize / 2;
    std::size_t numBins_ = kDefaultFFTSize / 2 + 1;
    float frameRate_ = 86.13f;
    bool prepared_ = false;

    // STFT components
    STFT stft_;
    OverlapAdd overlapAdd_;
    SpectralBuffer inputSpectrum_;
    SpectralBuffer outputSpectrum_;

    // Parameters (user-facing values)
    float thresholdDb_ = kDefaultThresholdDb;
    float ratio_ = kDefaultRatio;
    float attackMs_ = kDefaultAttackMs;
    float releaseMs_ = kDefaultReleaseMs;
    float lowHz_ = kMinFrequencyHz;
    float highHz_ = kMaxFrequencyHz;
    float smearAmount_ = kMinSmearAmount;

    // Computed values
    float attackCoeff_ = 0.0f;
    float releaseCoeff_ = 0.0f;
    std::size_t lowBin_ = 0;
    std::size_t highBin_ = 0;
    std::size_t smearKernelSize_ = 1;

    // Parameter smoothing
    OnePoleSmoother thresholdSmoother_;
    OnePoleSmoother ratioSmoother_;

    // Per-bin state
    std::vector<float> binEnvelopes_;
    std::vector<float> gateGains_;
    std::vector<float> smearedGains_;

    // Auxiliary buffers
    std::vector<float> zeroBuffer_;
    std::vector<float> singleSampleInputBuffer_;
    std::vector<float> singleSampleOutputBuffer_;
    std::size_t singleSampleWritePos_ = 0;
    std::size_t singleSampleReadPos_ = 0;
};

} // namespace DSP
} // namespace Krate
