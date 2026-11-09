// ==============================================================================
// Crossover Network for Multi-Band Processing
// ==============================================================================
// Multi-band crossover network for 1-4 bands using cascaded CrossoverLR4.
// Real-time safe: fixed-size arrays, no allocations in process().
//
// Phase Compensation:
// Uses D'Appolito allpass compensation method for flat frequency response.
// Each band is phase-aligned by adding allpass filters at all crossover
// frequencies HIGHER than its own split point:
// - Band 0: LP(f0) * AP(f1) * AP(f2) * ... * AP(fN-2)
// - Band k: HP(f0..fk-1) * LP(fk) * AP(fk+1) * ... * AP(fN-2)
// - Band N-1: HP(f0) * HP(f1) * ... * HP(fN-2)
//
// Reference: D'Appolito, "Active Realization of Multiway All-Pass Crossover
// Systems", Journal of the Audio Engineering Society, Vol. 35, No. 4, 1987.
//
// References:
// - specs/002-band-management/contracts/crossover_network_api.md
// - specs/002-band-management/spec.md FR-001 to FR-014, SC-001
// - Constitution Principle XIV: Reuse Krate::DSP::CrossoverLR4
// ==============================================================================

#pragma once

#include <krate/dsp/processors/crossover_filter.h>
#include <krate/dsp/primitives/biquad.h>
#include "band_state.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace Disrumpo {

/// @brief Multi-band crossover network for 1-4 bands.
/// Uses cascaded CrossoverLR4 instances with D'Appolito allpass compensation
/// per Constitution Principle XIV. Achieves SC-001 (+/-0.1dB flat response).
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

    /// Q for 2nd-order allpass to match LR4 phase response
    /// LR4 = two cascaded Butterworth (Q=0.7071) filters
    /// The allpass must have the same Q to match the phase curve
    /// Reference: KVR "N-band Linkwitz-Riley crossovers" discussion
    static constexpr float kAllpassQ = 0.7071f;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    CrossoverNetwork() noexcept = default;
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

        // CrossoverLR4::prepare() snaps its frequency smoother to the stored
        // frequency, so set the frequency (clamped at the NEW rate, hence the
        // first prepare) and prepare again: no glide from the previous value
        for (int i = 0; i < kMaxBands - 1; ++i) {
            crossovers_[i].prepare(sampleRate);
            crossovers_[i].setCrossoverFrequency(crossoverFrequencies_[i]);
            crossovers_[i].prepare(sampleRate);
        }

        // Initialize D'Appolito allpass compensation
        updateAllpassCoefficients();
    }

    /// @brief Reset all filter states without reinitialization.
    void reset() noexcept {
        for (auto& crossover : crossovers_) {
            crossover.reset();
        }
        resetAllpasses();
    }

    // =========================================================================
    // Configuration
    // =========================================================================

    /// @brief Change band count dynamically.
    /// Preserves existing crossover positions per FR-011a/FR-011b.
    /// @param numBands New number of bands (1-4)
    void setBandCount(int numBands) noexcept {
        debugSetBandCountCalls.fetch_add(1, std::memory_order_relaxed);
        const int newBandCount = clampBandCount(numBands);
        if (newBandCount == numBands_) {
            return;
        }

        const int oldBandCount = numBands_;
        numBands_ = newBandCount;

        // Redistribute crossovers per FR-011a/FR-011b
        redistributeCrossovers(oldBandCount, newBandCount);

        // Crossovers and allpasses joining the chain start clean at their frequency:
        // never the state they froze with when last used, nor a glide from a stale
        // frequency (D-023). CrossoverLR4::prepare only snaps the smoother, sets the
        // coefficients and clears the state: no allocation.
        if (newBandCount > oldBandCount && prepared_) {
            const int firstNew = std::max(oldBandCount - 1, 0);
            for (int i = firstNew; i < newBandCount - 1; ++i) {
                crossovers_[i].prepare(sampleRate_);
            }
            for (int band = 0; band < kMaxBands - 2; ++band) {
                for (int apIdx = std::max(band + 1, firstNew); apIdx < kMaxBands - 1; ++apIdx) {
                    allpasses_[band][apIdx - band - 1].reset();
                }
            }
            for (int i = firstNew; i < kMaxBands - 1; ++i) {
                dryAllpasses_[i].reset();
            }
        }
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

        const float clamped = clampFrequency(hz);
        crossoverFrequencies_[index] = clamped;
        frequencyWasSet_[index] = true;
        crossovers_[index].setCrossoverFrequency(clamped);

        // Update allpass filters that use this crossover frequency
        // Crossover index i is used as allpass for bands 0..(i-1) and for the dry path
        if (prepared_) {
            const float srFloat = static_cast<float>(sampleRate_);
            dryAllpasses_[index].configure(
                Krate::DSP::FilterType::Allpass,
                clamped,
                kAllpassQ,
                0.0f,
                srFloat
            );
            for (int band = 0; band < index; ++band) {
                const int apIdx = index - band - 1;
                allpasses_[band][apIdx].configure(
                    Krate::DSP::FilterType::Allpass,
                    clamped,
                    kAllpassQ,
                    0.0f,
                    srFloat
                );
            }
        }
    }

    // =========================================================================
    // Queries
    // =========================================================================

    /// @brief Get current band count.
    [[nodiscard]] int getBandCount() const noexcept {
        return numBands_;
    }

    /// @brief Get crossover frequency at index.
    [[nodiscard]] float getCrossoverFrequency(int index) const noexcept {
        if (index < 0 || index >= kMaxBands - 1) {
            return 1000.0f; // Default
        }
        return crossoverFrequencies_[index];
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
        if (!prepared_) {
            // Output zeros if not prepared
            for (int i = 0; i < kMaxBands; ++i) {
                bands[i] = 0.0f;
            }
            return;
        }

        // FR-014: For 1 band, pass input directly
        if (numBands_ == 1) {
            bands[0] = input;
            return;
        }

        // FR-012: Cascaded band splitting with D'Appolito allpass compensation
        // Input -> Split1 -> (Band0, Remainder) -> Split2 -> (Band1, Remainder) -> ...
        //
        // Phase compensation pattern:
        // Band 0: LP(f0) * AP(f1) * AP(f2) * ... * AP(fN-2)
        // Band 1: HP(f0) * LP(f1) * AP(f2) * ... * AP(fN-2)
        // Band k: HP(f0..fk-1) * LP(fk) * AP(fk+1) * ... * AP(fN-2)
        // Band N-1: HP(f0) * HP(f1) * ... * HP(fN-2)

        float remainder = input;

        for (int i = 0; i < numBands_ - 1; ++i) {
            auto outputs = crossovers_[i].process(remainder);
            bands[i] = outputs.low;       // Low band goes to output
            remainder = outputs.high;     // High band continues down the chain
        }

        // Last band gets the final remainder (no allpass needed)
        bands[numBands_ - 1] = remainder;

        // Apply D'Appolito allpass compensation to each band
        // Band k needs allpasses at frequencies f[k+1], f[k+2], ..., f[N-2]
        for (int band = 0; band < numBands_ - 1; ++band) {
            // Apply allpasses for crossovers higher than this band's split
            for (int apIdx = band + 1; apIdx < numBands_ - 1; ++apIdx) {
                bands[band] = allpasses_[band][apIdx - band - 1].process(bands[band]);
            }
        }
    }

    /// @brief Give a dry (unsplit) sample the phase of the summed bands (D-018).
    /// The clean band sum is AP(f0) * AP(f1) * ... * AP(fN-2) * x (each LR4
    /// LP + HP pair is a 2nd-order allpass at its crossover), so a dry signal
    /// mixed with the wet sum must run through the same allpass chain or every
    /// crossover notches. Call once per sample, alongside process().
    /// @param x Dry input sample
    /// @return x through the allpasses at every active crossover frequency
    [[nodiscard]] float processDryCompensation(float x) noexcept {
        if (!prepared_ || numBands_ == 1) {
            return x;
        }
        for (int i = 0; i < numBands_ - 1; ++i) {
            x = dryAllpasses_[i].process(x);
        }
        return x;
    }

private:
    // =========================================================================
    // Helper Functions
    // =========================================================================

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
            if (frequencyWasSet_[i]) {
                continue;
            }
            const float logFreq = logMin + step * static_cast<float>(i + 1);
            const float freq = std::pow(10.0f, logFreq);
            crossoverFrequencies_[i] = clampFrequency(freq);
            crossovers_[i].setCrossoverFrequency(crossoverFrequencies_[i]);
        }
        frequenciesInitialized_ = true;
    }

    /// @brief Redistribute crossovers when band count changes.
    /// FR-011a: Preserve existing crossovers when increasing
    /// FR-011b: Preserve lowest N-1 crossovers when decreasing
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
            updateAllpassCoefficients();
            return;
        }

        if (newBandCount > oldBandCount) {
            // FR-011a: Increasing - preserve existing, fill the free indices
            redistributeIncreasing(oldBandCount, newBandCount);
            updateAllpassCoefficients();
        } else {
            // FR-011b: Decreasing - keep lowest N-1 crossovers
            // The existing frequencies stay in place, we just use fewer
            // Update allpasses for new band configuration
            updateAllpassCoefficients();
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
            if (frequencyWasSet_[i]) {
                ++i;
                continue;
            }
            int end = i;
            while (end < used && !frequencyWasSet_[end]) {
                ++end;
            }
            const float lower = (i == 0) ? kMinCrossoverHz : crossoverFrequencies_[i - 1];
            const float upper = (end < used) ? crossoverFrequencies_[end] : kMaxCrossoverHz;
            const auto parts = static_cast<float>(end - i + 1);
            for (int k = i; k < end; ++k) {
                const float frac = static_cast<float>(k - i + 1) / parts;
                crossoverFrequencies_[k] = clampFrequency(lower * std::pow(upper / lower, frac));
                crossovers_[k].setCrossoverFrequency(crossoverFrequencies_[k]);
            }
            i = end;
        }
    }

    // =========================================================================
    // Allpass Helpers
    // =========================================================================

    /// @brief Update all allpass filter coefficients.
    /// Called after crossover frequencies change.
    void updateAllpassCoefficients() noexcept {
        const float srFloat = static_cast<float>(sampleRate_);

        // For each band, configure allpasses at higher crossover frequencies
        // Band k needs allpasses at f[k+1], f[k+2], ..., f[N-2]
        for (int band = 0; band < kMaxBands - 2; ++band) {
            for (int apIdx = band + 1; apIdx < kMaxBands - 1; ++apIdx) {
                const float freq = crossoverFrequencies_[apIdx];
                allpasses_[band][apIdx - band - 1].configure(
                    Krate::DSP::FilterType::Allpass,
                    clampFrequency(freq),
                    kAllpassQ,
                    0.0f,
                    srFloat
                );
            }
        }

        // Dry-path compensation: one allpass at every crossover frequency
        for (int i = 0; i < kMaxBands - 1; ++i) {
            dryAllpasses_[i].configure(
                Krate::DSP::FilterType::Allpass,
                clampFrequency(crossoverFrequencies_[i]),
                kAllpassQ,
                0.0f,
                srFloat
            );
        }
    }

    /// @brief Reset all allpass filter states.
    void resetAllpasses() noexcept {
        for (auto& bandAllpasses : allpasses_) {
            for (auto& ap : bandAllpasses) {
                ap.reset();
            }
        }
        for (auto& ap : dryAllpasses_) {
            ap.reset();
        }
    }

    // =========================================================================
    // Members
    // =========================================================================

    double sampleRate_ = 44100.0;
    int numBands_ = kDefaultBands;
    bool prepared_ = false;

    // N-1 crossovers for N bands
    std::array<Krate::DSP::CrossoverLR4, kMaxBands - 1> crossovers_;

    // D'Appolito allpass compensation filters
    // allpasses_[band][apIdx] is the allpass for band 'band' at crossover 'band + apIdx + 1'
    // Band 0 needs up to 2 allpasses (at f1..f2 for 4 bands)
    // Band 1 needs up to 1 allpass (at f2)
    // Band 2 and 3 need no allpasses
    // Maximum allpasses per band = kMaxBands - 2 = 2
    std::array<std::array<Krate::DSP::Biquad, kMaxBands - 2>, kMaxBands - 2> allpasses_;

    // Dry-path allpasses: dryAllpasses_[i] at crossover i (processDryCompensation)
    std::array<Krate::DSP::Biquad, kMaxBands - 1> dryAllpasses_;

    // Target frequencies (for redistribution logic)
    std::array<float, kMaxBands - 1> crossoverFrequencies_ = {
        200.0f, 1500.0f, 6000.0f
    };

    // Crossovers given a value through setCrossoverFrequency()
    std::array<bool, kMaxBands - 1> frequencyWasSet_{};

    // True once a logarithmic distribution has been computed (first prepare
    // with more than one band); later prepares keep the stored frequencies
    bool frequenciesInitialized_ = false;
};

} // namespace Disrumpo
