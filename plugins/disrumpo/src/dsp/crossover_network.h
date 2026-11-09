// ==============================================================================
// Crossover Network for Multi-Band Processing
// ==============================================================================
// Multi-band crossover network for 1-4 bands: Disrumpo's band policy over one
// Krate::DSP::MultibandCrossover<4> (cascaded CrossoverLR4, D'Appolito allpass
// compensation, dry-path compensation).
// Real-time safe: fixed-size arrays, no allocations in process().
//
// Phase Compensation (in MultibandCrossover):
// Each band is phase-aligned by adding allpass filters at all crossover
// frequencies HIGHER than its own split point:
// - Band 0: LP(f0) * AP(f1) * AP(f2) * ... * AP(fN-2)
// - Band k: HP(f0..fk-1) * LP(fk) * AP(fk+1) * ... * AP(fN-2)
// - Band N-1: HP(f0) * HP(f1) * ... * HP(fN-2)
//
// Reference: D'Appolito, "Active Realization of Multiway All-Pass Crossover
// Systems", Journal of the Audio Engineering Society, Vol. 35, No. 4, 1987.
//
// Policy kept here: band count 1-4, crossover range 20 Hz - 20 kHz
// (state_codec.h) and 0.45 * sampleRate, logarithmic defaults on the first
// prepare, redistribution on a band-count change (FR-011a/FR-011b, D-023).
//
// References:
// - specs/002-band-management/contracts/crossover_network_api.md
// - specs/002-band-management/spec.md FR-001 to FR-014, SC-001
// - Constitution Principle XIV: Reuse Krate::DSP::MultibandCrossover
// ==============================================================================

#pragma once

#include <krate/dsp/processors/crossover_filter.h>
#include "band_state.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Disrumpo {

/// @brief Multi-band crossover network for 1-4 bands.
/// Wraps Krate::DSP::MultibandCrossover<4> with compensation on, per
/// Constitution Principle XIV. Achieves SC-001 (+/-0.1dB flat response).
/// Real-time safe: fixed-size arrays, no allocations in process().
class CrossoverNetwork {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr int kMaxBands = 4;
    static constexpr int kMinBands = 1;
    static constexpr int kDefaultBands = 4;

    /// @brief Test probe (D-003): setBandCount() calls over all instances, so a test
    /// can tell which thread or call reconfigured the live networks.
    static inline std::atomic<std::uint64_t> debugSetBandCountCalls{0};
    static constexpr float kDefaultSmoothingMs = 10.0f;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    CrossoverNetwork() noexcept {
        network_.setAllpassCompensation(true);
        for (int i = 0; i < kMaxBands - 1; ++i) {
            network_.setCrossoverFrequency(i, kInitialFrequencies[idx(i)]);
        }
    }
    ~CrossoverNetwork() noexcept = default;

    // Non-copyable (contains filter state)
    CrossoverNetwork(const CrossoverNetwork&) = delete;
    CrossoverNetwork& operator=(const CrossoverNetwork&) = delete;

    // Move operations
    CrossoverNetwork(CrossoverNetwork&&) noexcept = default;
    CrossoverNetwork& operator=(CrossoverNetwork&&) noexcept = default;

    // =========================================================================
    // Initialization (FR-003, FR-004)
    // =========================================================================

    /// @brief Initialize for given sample rate and band count.
    /// @param sampleRate Sample rate in Hz
    /// @param numBands Number of bands (1-4)
    void prepare(double sampleRate, int numBands) noexcept {
        sampleRate_ = sampleRate;
        numBands_ = clampBandCount(numBands);
        prepared_ = true;

        // Logarithmic defaults only on the first prepare, and only for
        // crossovers nobody has set. A re-prepare (sample-rate or block-size
        // change) keeps the stored frequencies (D-046)
        if (!frequenciesInitialized_) {
            initializeLogarithmicDistribution();
        }

        // MultibandCrossover::prepare() snaps every crossover to its stored
        // frequency at the new rate: no glide from the previous value
        network_.setActiveBands(numBands_);
        network_.prepare(sampleRate);
    }

    /// @brief Reset all filter states without reinitialization.
    void reset() noexcept {
        network_.reset();
    }

    // =========================================================================
    // Configuration
    // =========================================================================

    /// @brief Change band count dynamically.
    /// Preserves existing crossover positions per FR-011a/FR-011b.
    /// Crossovers and allpasses joining the chain start clean at their
    /// frequency (D-023; MultibandCrossover::setActiveBands).
    /// @param numBands New number of bands (1-4)
    void setBandCount(int numBands) noexcept {
        debugSetBandCountCalls.fetch_add(1, std::memory_order_relaxed);
        const int newBandCount = clampBandCount(numBands);
        if (newBandCount == numBands_) {
            return;
        }

        const int oldBandCount = numBands_;
        numBands_ = newBandCount;

        // Redistribute crossovers per FR-011a/FR-011b, then let the joining
        // ones start clean at those frequencies
        redistributeCrossovers(oldBandCount, newBandCount);
        network_.setActiveBands(newBandCount);
    }

    /// @brief Set crossover frequency for a specific split point.
    /// The value is stored for every index, including crossovers the current
    /// band count does not use, so a later band-count increase finds it (D-022).
    /// @param index Crossover index (0 to kMaxBands-2)
    /// @param hz Frequency in Hz
    void setCrossoverFrequency(int index, float hz) noexcept {
        if (index < 0 || index >= kMaxBands - 1) {
            return;
        }
        frequencyWasSet_[idx(index)] = true;
        network_.setCrossoverFrequency(index, clampFrequency(hz));
    }

    // =========================================================================
    // Queries
    // =========================================================================

    /// @brief Get current band count.
    [[nodiscard]] int getBandCount() const noexcept {
        return numBands_;
    }

    /// @brief Get crossover frequency at index (1000 Hz for an invalid index).
    [[nodiscard]] float getCrossoverFrequency(int index) const noexcept {
        return network_.getCrossoverFrequency(index);
    }

    /// @brief Check if prepare() has been called.
    [[nodiscard]] bool isPrepared() const noexcept {
        return prepared_;
    }

    // =========================================================================
    // Processing (FR-001a, FR-012, FR-013, FR-014, SC-001)
    // =========================================================================

    /// @brief Process single sample, output to band array.
    /// For 1 band: passes input directly to bands[0].
    /// For N bands: cascaded split with D'Appolito allpass compensation.
    /// SC-001: Achieves +/-0.1dB flat frequency response when bands summed.
    /// @param input Input sample
    /// @param bands Output array (uses first numBands_ elements)
    void process(float input, std::array<float, kMaxBands>& bands) noexcept {
        network_.process(input, bands);
    }

    /// @brief Give a dry (unsplit) sample the phase of the summed bands (D-018).
    /// The clean band sum is AP(f0) * AP(f1) * ... * AP(fN-2) * x (each LR4
    /// LP + HP pair is a 2nd-order allpass at its crossover), so a dry signal
    /// mixed with the wet sum must run through the same allpass chain or every
    /// crossover notches. Call once per sample, alongside process().
    /// @param x Dry input sample
    /// @return x through the allpasses at every active crossover frequency
    [[nodiscard]] float processDryCompensation(float x) noexcept {
        return network_.processDryCompensation(x);
    }

private:
    // =========================================================================
    // Helper Functions
    // =========================================================================

    [[nodiscard]] static constexpr std::size_t idx(int i) noexcept {
        return static_cast<std::size_t>(i);
    }

    /// @brief Clamp band count to valid range.
    [[nodiscard]] static int clampBandCount(int numBands) noexcept {
        if (numBands < kMinBands) return kMinBands;
        if (numBands > kMaxBands) return kMaxBands;
        return numBands;
    }

    /// @brief Clamp frequency to valid range.
    [[nodiscard]] float clampFrequency(float freq) const noexcept {
        const float maxFreq = static_cast<float>(sampleRate_) * 0.45f;
        if (freq < kMinCrossoverHz) return kMinCrossoverHz;
        if (freq > maxFreq) return maxFreq;
        if (freq > kMaxCrossoverHz) return kMaxCrossoverHz;
        return freq;
    }

    /// @brief Initialize crossover frequencies with logarithmic distribution.
    /// FR-009: Crossover frequencies redistribute logarithmically across 20Hz-20kHz
    /// Crossovers already set through setCrossoverFrequency() keep their value.
    void initializeLogarithmicDistribution() noexcept {
        if (numBands_ <= 1) {
            return;
        }

        // Logarithmic distribution from 20Hz to 20kHz
        const float logMin = std::log10(kMinCrossoverHz);      // ~1.301
        const float logMax = std::log10(kMaxCrossoverHz);      // ~4.301
        const float step = (logMax - logMin) / static_cast<float>(numBands_);

        for (int i = 0; i < numBands_ - 1; ++i) {
            if (frequencyWasSet_[idx(i)]) {
                continue;
            }
            const float logFreq = logMin + step * static_cast<float>(i + 1);
            network_.setCrossoverFrequency(i, clampFrequency(std::pow(10.0f, logFreq)));
        }
        frequenciesInitialized_ = true;
    }

    /// @brief Redistribute crossovers when band count changes.
    /// FR-011a: Preserve existing crossovers when increasing
    /// FR-011b: Preserve lowest N-1 crossovers when decreasing (the stored
    /// frequencies stay in place; fewer are used)
    void redistributeCrossovers(int oldBandCount, int newBandCount) noexcept {
        if (newBandCount <= 1) {
            // No crossovers needed for 1 band
            return;
        }

        if (oldBandCount <= 1) {
            // Was 1 band, now more: logarithmic defaults only if no
            // distribution exists yet; otherwise keep the stored values (FR-011a)
            if (!frequenciesInitialized_) {
                initializeLogarithmicDistribution();
            }
            return;
        }

        if (newBandCount > oldBandCount) {
            // FR-011a: Increasing - preserve existing, fill the free indices
            redistributeIncreasing(oldBandCount, newBandCount);
        }
    }

    /// @brief Fill the crossovers a band-count increase adds (FR-011a, D-023).
    /// Running crossovers keep their index and frequency, so crossover i stays the
    /// split the crossover parameter i sets and a later decrease returns to the same
    /// split. A joining crossover set explicitly while it was idle (state restore or
    /// automation) keeps that value. Each run of new ones splits the gap between the
    /// crossover below and the next set one above (else 20 kHz) at even log spacing.
    void redistributeIncreasing(int oldBandCount, int newBandCount) noexcept {
        const int used = newBandCount - 1;
        int i = std::max(oldBandCount - 1, 0);
        while (i < used) {
            if (frequencyWasSet_[idx(i)]) {
                ++i;
                continue;
            }
            int end = i;
            while (end < used && !frequencyWasSet_[idx(end)]) {
                ++end;
            }
            const float lower = (i == 0) ? kMinCrossoverHz : network_.getCrossoverFrequency(i - 1);
            const float upper = (end < used) ? network_.getCrossoverFrequency(end) : kMaxCrossoverHz;
            const auto parts = static_cast<float>(end - i + 1);
            for (int k = i; k < end; ++k) {
                const float frac = static_cast<float>(k - i + 1) / parts;
                network_.setCrossoverFrequency(k, clampFrequency(lower * std::pow(upper / lower, frac)));
            }
            i = end;
        }
    }

    // =========================================================================
    // Members
    // =========================================================================

    /// Frequencies before the first prepare (replaced by the logarithmic
    /// defaults there, unless set)
    static constexpr std::array<float, kMaxBands - 1> kInitialFrequencies{200.0f, 1500.0f, 6000.0f};

    double sampleRate_ = 44100.0;
    int numBands_ = kDefaultBands;
    bool prepared_ = false;

    // The split: N-1 LR4 crossovers, band and dry-path allpass compensation
    Krate::DSP::MultibandCrossover<kMaxBands> network_;

    // Crossovers given a value through setCrossoverFrequency()
    std::array<bool, kMaxBands - 1> frequencyWasSet_{};

    // True once a logarithmic distribution has been computed (first prepare
    // with more than one band); later prepares keep the stored frequencies
    bool frequenciesInitialized_ = false;
};

} // namespace Disrumpo
