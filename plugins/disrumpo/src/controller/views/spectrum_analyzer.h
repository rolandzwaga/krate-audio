#pragma once

// ==============================================================================
// SpectrumAnalyzer - UI-Thread FFT Processor
// ==============================================================================
// Performs windowed FFT analysis on audio samples received via SpectrumFIFO.
// All processing runs on the UI thread. Provides smoothed dB magnitudes and
// peak hold values for spectrum display rendering.
//
// Uses existing Krate::DSP::FFT and Krate::DSP::generateHann().
// All memory pre-allocated in prepare(). No allocations during process().
// ==============================================================================

#include <krate/dsp/primitives/fft.h>
#include <krate/dsp/primitives/spectrum_fifo.h>
#include <krate/dsp/core/window_functions.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace Disrumpo {

/// @brief Configuration for spectrum analyzer
struct SpectrumConfig {
    size_t fftSize = 2048;          ///< FFT window size (power of 2)
    size_t scopeSize = 512;         ///< Number of display points
    float smoothingAttack = 0.9f;   ///< Attack coefficient (0-1, higher = slower)
    float smoothingRelease = 0.7f;  ///< Release coefficient (0-1, higher = slower)
    float peakHoldTime = 1.0f;      ///< Peak hold duration in seconds
    float peakFallRate = 12.0f;     ///< Peak decay rate in dB/s
    float minDb = -96.0f;           ///< Floor dB level
    float maxDb = 0.0f;             ///< Ceiling dB level
    float sampleRate = 44100.0f;    ///< Audio sample rate
};

/// @brief UI-thread spectrum analyzer processor.
///
/// Takes audio samples from a SpectrumFIFO, performs windowed FFT,
/// converts to dB magnitudes, applies attack/release smoothing and
/// peak hold with decay.
///
/// @note All memory pre-allocated in prepare(). No allocations during process().
class SpectrumAnalyzer {
public:
    SpectrumAnalyzer() = default;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Initialize analyzer with configuration.
    /// @note Allocates memory - call only during setup, not real-time.
    void prepare(const SpectrumConfig& config) {
        config_ = config;

        // Prepare FFT engine
        fft_.prepare(config.fftSize);

        // Generate Hann window coefficients
        hannWindow_.resize(config.fftSize);
        Krate::DSP::Window::generateHann(hannWindow_.data(), config.fftSize);

        // Allocate working buffers
        windowedSamples_.resize(config.fftSize, 0.0f);
        fftOutput_.resize(fft_.numBins());

        // Allocate display buffers (scope-sized)
        rawDecimated_.resize(config.scopeSize, config.minDb);
        smoothedDb_.resize(config.scopeSize, config.minDb);
        peakDb_.resize(config.scopeSize, config.minDb);
        peakHoldCountdown_.resize(config.scopeSize, 0.0f);

        // Pre-compute logarithmic frequency bin mapping
        precomputeBinMapping();

        prepared_ = true;
    }

    /// @brief Reset all display state (smoothed values and peaks) to floor.
    void reset() {
        if (!prepared_) return;
        std::fill(rawDecimated_.begin(), rawDecimated_.end(), config_.minDb);
        std::fill(smoothedDb_.begin(), smoothedDb_.end(), config_.minDb);
        std::fill(peakDb_.begin(), peakDb_.end(), config_.minDb);
        std::fill(peakHoldCountdown_.begin(), peakHoldCountdown_.end(), 0.0f);
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process new data from FIFO and update spectrum.
    /// @param fifo Pointer to the FIFO to read from (may be null)
    /// @param deltaTimeSec Time since last call in seconds (for peak decay)
    /// @return true if new FFT data was computed
    bool process(Krate::DSP::SpectrumFIFO<8192>* fifo, float deltaTimeSec) {
        if (!prepared_) return false;

        if (!fifo || fifo->totalWritten() < config_.fftSize) {
            // Not enough data yet; still decay peaks and smoothed values
            decayAll(deltaTimeSec);
            return false;
        }

        // Read latest fftSize samples from FIFO
        if (fifo->readLatest(windowedSamples_.data(), config_.fftSize) == 0) {
            decayAll(deltaTimeSec);
            return false;
        }

        // Apply Hann window
        for (size_t i = 0; i < config_.fftSize; ++i) {
            windowedSamples_[i] *= hannWindow_[i];
        }

        // Forward FFT: real -> complex
        fft_.forward(windowedSamples_.data(), fftOutput_.data());

        // Decimate FFT bins to scope size (logarithmic mapping)
        decimateToScope();

        // Apply smoothing (attack/release) and update peaks
        applySmoothingAndPeaks(deltaTimeSec);

        return true;
    }

    // =========================================================================
    // Accessors
    // =========================================================================

    /// @brief Get smoothed dB values for rendering (scope-sized).
    [[nodiscard]] const std::vector<float>& getSmoothedDb() const { return smoothedDb_; }

    /// @brief Get peak hold dB values for rendering (scope-sized).
    [[nodiscard]] const std::vector<float>& getPeakDb() const { return peakDb_; }

    /// @brief Get the frequency corresponding to a scope index.
    /// @param index Scope index [0, scopeSize)
    /// @return Frequency in Hz (logarithmic mapping 20Hz to 20kHz)
    [[nodiscard]] float scopeIndexToFreq(size_t index) const {
        if (config_.scopeSize <= 1) return 20.0f;
        float t = static_cast<float>(index) / static_cast<float>(config_.scopeSize - 1);
        // 20 * 1000^t maps [0,1] to [20, 20000]
        return 20.0f * std::pow(1000.0f, t);
    }

    /// @brief Get the scope index corresponding to a frequency.
    /// @param freqHz Frequency in Hz [20, 20000]
    /// @return Scope index (may be fractional)
    [[nodiscard]] float freqToScopeIndex(float freqHz) const {
        if (config_.scopeSize <= 1 || freqHz <= 20.0f) return 0.0f;
        if (freqHz >= 20000.0f) return static_cast<float>(config_.scopeSize - 1);
        // Inverse of scopeIndexToFreq: t = log(freq/20) / log(1000)
        float t = std::log(freqHz / 20.0f) / std::log(1000.0f);
        return t * static_cast<float>(config_.scopeSize - 1);
    }

    /// @brief Get the current configuration.
    [[nodiscard]] const SpectrumConfig& config() const { return config_; }

    /// @brief Check if prepare() has been called.
    [[nodiscard]] bool isPrepared() const { return prepared_; }

private:
    // =========================================================================
    // Internal Methods
    // =========================================================================

    /// @brief Pre-compute FFT bin to scope point mapping tables.
    void precomputeBinMapping() {
        const size_t numBins = fft_.numBins();
        const float binHz = config_.sampleRate / static_cast<float>(config_.fftSize);

        // For each scope point, determine the range of FFT bins that map to it
        scopeBinLow_.resize(config_.scopeSize);
        scopeBinHigh_.resize(config_.scopeSize);

        for (size_t s = 0; s < config_.scopeSize; ++s) {
            // Get frequency boundaries for this scope point (midpoints to neighbors)
            float freqLow, freqHigh;

            if (s == 0) {
                freqLow = scopeIndexToFreq(0);
                freqHigh = std::sqrt(scopeIndexToFreq(0) * scopeIndexToFreq(1));
            } else if (s == config_.scopeSize - 1) {
                freqLow = std::sqrt(scopeIndexToFreq(s - 1) * scopeIndexToFreq(s));
                freqHigh = scopeIndexToFreq(s);
            } else {
                freqLow = std::sqrt(scopeIndexToFreq(s - 1) * scopeIndexToFreq(s));
                freqHigh = std::sqrt(scopeIndexToFreq(s) * scopeIndexToFreq(s + 1));
            }

            // Map to FFT bin indices
            size_t binLow = static_cast<size_t>(std::max(1.0f, freqLow / binHz));
            size_t binHigh = static_cast<size_t>(std::min(
                static_cast<float>(numBins - 1), freqHigh / binHz));

            // Ensure at least one bin
            if (binHigh < binLow) binHigh = binLow;
            if (binLow >= numBins) binLow = numBins - 1;
            if (binHigh >= numBins) binHigh = numBins - 1;

            scopeBinLow_[s] = binLow;
            scopeBinHigh_[s] = binHigh;
        }
    }

    /// @brief Decimate FFT bins to scope display points.
    /// Uses max-per-bin-range for peak preservation.
    void decimateToScope() {
        // FFT magnitude normalization: 2/N for single-sided spectrum
        const float normFactor = 2.0f / static_cast<float>(config_.fftSize);

        for (size_t s = 0; s < config_.scopeSize; ++s) {
            float maxMag = 0.0f;

            for (size_t b = scopeBinLow_[s]; b <= scopeBinHigh_[s]; ++b) {
                float mag = fftOutput_[b].magnitude() * normFactor;
                if (mag > maxMag) maxMag = mag;
            }

            // Convert to dB
            if (maxMag < 1e-10f) maxMag = 1e-10f;  // Avoid log(0)
            rawDecimated_[s] = 20.0f * std::log10(maxMag);
        }
    }

    /// @brief Apply attack/release smoothing and update peak hold.
    void applySmoothingAndPeaks(float deltaTimeSec) {
        for (size_t s = 0; s < config_.scopeSize; ++s) {
            const float newVal = rawDecimated_[s];
            const float oldVal = smoothedDb_[s];

            // One-pole smoothing with separate attack/release
            // Attack when signal rises, release when it falls
            if (newVal > oldVal) {
                // Attack: faster response to rising signal
                smoothedDb_[s] = oldVal + (newVal - oldVal) * (1.0f - config_.smoothingAttack);
            } else {
                // Release: slower decay
                smoothedDb_[s] = oldVal + (newVal - oldVal) * (1.0f - config_.smoothingRelease);
            }

            // Peak hold with timed decay
            if (smoothedDb_[s] > peakDb_[s]) {
                peakDb_[s] = smoothedDb_[s];
                peakHoldCountdown_[s] = config_.peakHoldTime;
            } else {
                peakHoldCountdown_[s] -= deltaTimeSec;
                if (peakHoldCountdown_[s] <= 0.0f) {
                    peakDb_[s] -= config_.peakFallRate * deltaTimeSec;
                    if (peakDb_[s] < config_.minDb) {
                        peakDb_[s] = config_.minDb;
                    }
                }
            }
        }
    }

    /// @brief Decay smoothed values and peaks when no new data available.
    void decayAll(float deltaTimeSec) {
        for (size_t s = 0; s < config_.scopeSize; ++s) {
            // Gradually decay smoothed values toward floor
            smoothedDb_[s] += (config_.minDb - smoothedDb_[s]) * 0.05f;

            // Decay peaks
            peakHoldCountdown_[s] -= deltaTimeSec;
            if (peakHoldCountdown_[s] <= 0.0f) {
                peakDb_[s] -= config_.peakFallRate * deltaTimeSec;
                if (peakDb_[s] < config_.minDb) {
                    peakDb_[s] = config_.minDb;
                }
            }
        }
    }

    // =========================================================================
    // State
    // =========================================================================

    bool prepared_ = false;
    SpectrumConfig config_;

    // FFT engine and window
    Krate::DSP::FFT fft_;
    std::vector<float> hannWindow_;
    std::vector<float> windowedSamples_;
    std::vector<Krate::DSP::Complex> fftOutput_;

    // Bin mapping (pre-computed in prepare)
    std::vector<size_t> scopeBinLow_;   ///< First FFT bin for each scope point
    std::vector<size_t> scopeBinHigh_;  ///< Last FFT bin for each scope point

    // Display buffers (scope-sized)
    std::vector<float> rawDecimated_;       ///< Raw dB values from current FFT
    std::vector<float> smoothedDb_;         ///< Smoothed dB values for rendering
    std::vector<float> peakDb_;             ///< Peak hold dB values
    std::vector<float> peakHoldCountdown_;  ///< Time remaining in peak hold (seconds)
};

} // namespace Disrumpo
