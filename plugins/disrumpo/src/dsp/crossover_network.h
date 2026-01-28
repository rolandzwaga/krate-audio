// ==============================================================================
// Crossover Network for Multi-Band Processing
// ==============================================================================
// Multi-band crossover network for 1-8 bands using cascaded CrossoverLR4.
// Real-time safe: fixed-size arrays, no allocations in process().
//
// References:
// - specs/002-band-management/contracts/crossover_network_api.md
// - specs/002-band-management/spec.md FR-001 to FR-014
// - Constitution Principle XIV: Reuse Krate::DSP::CrossoverLR4
// ==============================================================================

#pragma once

#include <krate/dsp/processors/crossover_filter.h>
#include "band_state.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Disrumpo {

/// @brief Multi-band crossover network for 1-8 bands.
/// Uses cascaded CrossoverLR4 instances per Constitution Principle XIV.
/// Real-time safe: fixed-size arrays, no allocations in process().
class CrossoverNetwork {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr int kMaxBands = 8;
    static constexpr int kMinBands = 1;
    static constexpr int kDefaultBands = 4;
    static constexpr float kDefaultSmoothingMs = 10.0f;

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
    /// @param numBands Number of bands (1-8)
    void prepare(double sampleRate, int numBands) noexcept {
        sampleRate_ = sampleRate;
        numBands_ = clampBandCount(numBands);
        prepared_ = true;

        // Prepare all crossovers
        for (auto& crossover : crossovers_) {
            crossover.prepare(sampleRate);
        }

        // Initialize with logarithmic distribution
        initializeLogarithmicDistribution();
    }

    /// @brief Reset all filter states without reinitialization.
    void reset() noexcept {
        for (auto& crossover : crossovers_) {
            crossover.reset();
        }
    }

    // =========================================================================
    // Configuration
    // =========================================================================

    /// @brief Change band count dynamically.
    /// Preserves existing crossover positions per FR-011a/FR-011b.
    /// @param numBands New number of bands (1-8)
    void setBandCount(int numBands) noexcept {
        const int newBandCount = clampBandCount(numBands);
        if (newBandCount == numBands_) {
            return;
        }

        const int oldBandCount = numBands_;
        numBands_ = newBandCount;

        // Redistribute crossovers per FR-011a/FR-011b
        redistributeCrossovers(oldBandCount, newBandCount);
    }

    /// @brief Set crossover frequency for a specific split point.
    /// @param index Crossover index (0 to numBands-2)
    /// @param hz Frequency in Hz
    void setCrossoverFrequency(int index, float hz) noexcept {
        if (index < 0 || index >= numBands_ - 1) {
            return;
        }

        const float clamped = clampFrequency(hz);
        crossoverFrequencies_[index] = clamped;
        crossovers_[index].setCrossoverFrequency(clamped);
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
    // Processing (FR-001a, FR-012, FR-013, FR-014)
    // =========================================================================

    /// @brief Process single sample, output to band array.
    /// For 1 band: passes input directly to bands[0].
    /// For N bands: cascaded split to bands[0..N-1].
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

        // FR-012: Cascaded band splitting
        // Input -> Split1 -> (Band0, Remainder) -> Split2 -> (Band1, Remainder) -> ...
        float remainder = input;

        for (int i = 0; i < numBands_ - 1; ++i) {
            auto outputs = crossovers_[i].process(remainder);
            bands[i] = outputs.low;      // Low band goes to output
            remainder = outputs.high;     // High band continues down the chain
        }

        // Last band gets the final remainder
        bands[numBands_ - 1] = remainder;
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
    void initializeLogarithmicDistribution() noexcept {
        if (numBands_ <= 1) {
            return;
        }

        // Logarithmic distribution from 20Hz to 20kHz
        const float logMin = std::log10(kMinCrossoverHz);      // ~1.301
        const float logMax = std::log10(kMaxCrossoverHz);      // ~4.301
        const float step = (logMax - logMin) / static_cast<float>(numBands_);

        for (int i = 0; i < numBands_ - 1; ++i) {
            const float logFreq = logMin + step * static_cast<float>(i + 1);
            const float freq = std::pow(10.0f, logFreq);
            crossoverFrequencies_[i] = clampFrequency(freq);
            crossovers_[i].setCrossoverFrequency(crossoverFrequencies_[i]);
        }
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
            // Was 1 band, now more - initialize fresh
            initializeLogarithmicDistribution();
            return;
        }

        if (newBandCount > oldBandCount) {
            // FR-011a: Increasing - preserve existing, insert new at logarithmic midpoints
            redistributeIncreasing(oldBandCount, newBandCount);
        } else {
            // FR-011b: Decreasing - keep lowest N-1 crossovers
            // The existing frequencies stay in place, we just use fewer
            // Nothing to do - crossoverFrequencies_ already has the values
        }
    }

    /// @brief Insert new crossovers at logarithmic midpoints when increasing band count.
    /// FR-011a: Preserve existing crossovers and add new ones
    void redistributeIncreasing(int oldBandCount, int newBandCount) noexcept {
        // Build list of band edges (20Hz, crossover1, crossover2, ..., 20kHz)
        std::array<float, kMaxBands + 1> edges{};
        edges[0] = kMinCrossoverHz;
        for (int i = 0; i < oldBandCount - 1; ++i) {
            edges[i + 1] = crossoverFrequencies_[i];
        }
        edges[oldBandCount] = kMaxCrossoverHz;
        const int numOldEdges = oldBandCount + 1;

        // Number of new crossovers to add
        const int toAdd = (newBandCount - 1) - (oldBandCount - 1);
        if (toAdd <= 0) return;

        // Collect all crossover frequencies (old + new)
        std::array<float, kMaxBands - 1> allFreqs{};
        int writeIdx = 0;

        // Copy existing crossovers
        for (int i = 0; i < oldBandCount - 1; ++i) {
            allFreqs[writeIdx++] = crossoverFrequencies_[i];
        }

        // Insert new crossovers at logarithmic midpoints of largest gaps
        for (int added = 0; added < toAdd; ++added) {
            // Find the largest logarithmic gap
            float maxLogGap = 0.0f;
            int bestGapIdx = 0;

            // Edges include: 20Hz, existing crossovers, new crossovers, 20kHz
            // We need to track all current edges
            std::array<float, kMaxBands + 1> currentEdges{};
            currentEdges[0] = kMinCrossoverHz;
            for (int i = 0; i < writeIdx; ++i) {
                currentEdges[i + 1] = allFreqs[i];
            }
            currentEdges[writeIdx + 1] = kMaxCrossoverHz;
            const int numCurrentEdges = writeIdx + 2;

            // Sort current edges
            std::sort(currentEdges.begin(), currentEdges.begin() + numCurrentEdges);

            // Find largest gap
            for (int i = 0; i < numCurrentEdges - 1; ++i) {
                float logGap = std::log10(currentEdges[i + 1]) - std::log10(currentEdges[i]);
                if (logGap > maxLogGap) {
                    maxLogGap = logGap;
                    bestGapIdx = i;
                }
            }

            // Insert at logarithmic midpoint
            const float newFreq = logMidpoint(currentEdges[bestGapIdx], currentEdges[bestGapIdx + 1]);
            allFreqs[writeIdx++] = clampFrequency(newFreq);
        }

        // Sort all frequencies in ascending order
        std::sort(allFreqs.begin(), allFreqs.begin() + writeIdx);

        // Store and apply to crossovers
        for (int i = 0; i < newBandCount - 1; ++i) {
            crossoverFrequencies_[i] = allFreqs[i];
            crossovers_[i].setCrossoverFrequency(crossoverFrequencies_[i]);
        }
    }

    /// @brief Calculate logarithmic midpoint between two frequencies.
    [[nodiscard]] static float logMidpoint(float f1, float f2) noexcept {
        const float logF1 = std::log10(f1);
        const float logF2 = std::log10(f2);
        return std::pow(10.0f, (logF1 + logF2) * 0.5f);
    }

    // =========================================================================
    // Members
    // =========================================================================

    double sampleRate_ = 44100.0;
    int numBands_ = kDefaultBands;
    bool prepared_ = false;

    // N-1 crossovers for N bands
    std::array<Krate::DSP::CrossoverLR4, kMaxBands - 1> crossovers_;

    // Target frequencies (for redistribution logic)
    std::array<float, kMaxBands - 1> crossoverFrequencies_ = {
        200.0f, 800.0f, 3200.0f, 8000.0f, 12000.0f, 16000.0f, 18000.0f
    };
};

} // namespace Disrumpo
