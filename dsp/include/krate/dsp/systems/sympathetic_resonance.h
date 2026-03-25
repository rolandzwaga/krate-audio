// ==============================================================================
// Layer 3: System Component - Sympathetic Resonance
// ==============================================================================
// Global shared resonance field for cross-voice harmonic reinforcement.
// Manages a pool of driven second-order resonators tuned to the union of
// all active voices' low-order partials.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, fixed-size arrays, no allocations)
// - Principle III: Modern C++ (RAII, constexpr, value semantics, C++20)
// - Principle IV: SIMD Optimization (SoA layout for SIMD-readiness)
// - Principle IX: Layer 3 (depends on Layer 0 math, Layer 1 Biquad/Smoother)
// - Principle XII: Test-First Development
//
// Reference: specs/132-sympathetic-resonance/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/primitives/biquad.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/systems/sympathetic_resonance_simd.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// Constants
// =============================================================================

/// Number of partials per voice for sympathetic resonance (compile-time).
/// 4 captures ~95% of audible sympathetic energy (Lehtonen et al. 2007).
inline constexpr int kSympatheticPartialCount = 4;

/// Maximum resonator pool capacity (standard mode).
inline constexpr int kMaxSympatheticResonators = 64;

/// Maximum resonator pool capacity (future Ultra mode).
inline constexpr int kMaxSympatheticResonatorsUltra = 128;

/// Frequency threshold for merging resonators (Hz).
inline constexpr float kMergeThresholdHz = 0.3f;

/// Reclaim threshold: -96 dB in linear scale.
inline constexpr float kReclaimThresholdLinear = 1.585e-5f;

/// Anti-mud HPF reference frequency (Hz).
inline constexpr float kAntiMudFreqRef = 100.0f;

/// Frequency-dependent Q reference frequency (Hz).
inline constexpr float kQFreqRef = 500.0f;

/// Minimum Q scaling factor (clamp floor for frequency-dependent Q).
inline constexpr float kMinQScale = 0.5f;

/// Maximum number of voice owners per resonator (for shared/merged tracking).
inline constexpr int kMaxOwnersPerResonator = 8;

// =============================================================================
// SympatheticPartialInfo
// =============================================================================

/// Per-voice partial frequency data passed on noteOn events.
struct SympatheticPartialInfo {
    /// Inharmonicity-adjusted partial frequencies (Hz), including fundamental as partial 1.
    std::array<float, kSympatheticPartialCount> frequencies{};
};

// =============================================================================
// SympatheticResonance
// =============================================================================

/// @brief Global shared resonance field for cross-voice harmonic reinforcement.
///
/// Manages a pool of driven second-order resonators tuned to the union of
/// all active voices' low-order partials. Fed by the global voice sum
/// (post polyphony gain compensation, pre master gain), the resonators produce
/// cross-voice harmonic reinforcement that is strongest for consonant intervals.
///
/// Signal flow:
///   Input: global voice sum (post polyphony gain compensation)
///   Output: sympathetic signal (post anti-mud filter, additive to master)
///
/// Lifecycle:
///   1. prepare(sampleRate) -- compute coefficients, no allocations
///   2. noteOn/noteOff      -- manage resonator pool (from MIDI handler)
///   3. process(input)      -- per-sample driven resonance + anti-mud HPF
///   4. reset()             -- clear all state
class SympatheticResonance {
public:
    SympatheticResonance() noexcept = default;

    // Non-copyable (contains filter state)
    SympatheticResonance(const SympatheticResonance&) = delete;
    SympatheticResonance& operator=(const SympatheticResonance&) = delete;
    SympatheticResonance(SympatheticResonance&&) noexcept = default;
    SympatheticResonance& operator=(SympatheticResonance&&) noexcept = default;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// Prepare for processing. Computes envelope coefficients, configures HPF.
    /// Must be called before process() or any noteOn/noteOff calls.
    /// @param sampleRate  Sample rate in Hz (44100-192000)
    void prepare(double sampleRate) noexcept {
        sampleRate_ = static_cast<float>(sampleRate);

        // Envelope follower release coefficient: 10ms release tau
        envelopeReleaseCoeff_ = std::exp(-1.0f / (0.010f * sampleRate_));

        // Configure anti-mud high-pass filter at reference frequency
        antiMudHpf_.configure(FilterType::Highpass, kAntiMudFreqRef,
                              kButterworthQ, 0.0f, sampleRate_);

        // Configure amount smoother: 5ms smoothing time
        amountSmoother_.configure(5.0f, sampleRate_);
        amountSmoother_.snapTo(0.0f);

        // Initialize default Q
        userQ_ = 316.2f; // Default: 100 * 10^0.5

        // Zero-initialize all pool arrays
        resetPool();
    }

    /// Reset all resonator state and filters. Keeps sample rate.
    void reset() noexcept {
        resetPool();
        antiMudHpf_.reset();
        amountSmoother_.reset();
        couplingGain_ = 0.0f;
    }

    // =========================================================================
    // Parameters (called once per audio block, before process loop)
    // =========================================================================

    /// Set the coupling amount (0.0 = bypassed, 1.0 = maximum coupling).
    /// Maps to approximately -46 dB (low) to -26 dB (high) coupling gain.
    /// Range calibrated per Lehtonen et al. (2007) piano sympathetic model
    /// (0.005-0.015 = -46 to -36 dB) with artistic headroom up to -26 dB.
    /// Smoothed internally to prevent clicks.
    /// @param amount  Normalized amount [0.0, 1.0]
    void setAmount(float amount) noexcept {
        if (amount == 0.0f) {
            couplingGain_ = 0.0f;
        } else {
            // Map [0,1] -> [-46, -26] dB -> linear
            couplingGain_ = std::pow(10.0f, (-46.0f + 20.0f * amount) / 20.0f);
        }
        amountSmoother_.setTarget(couplingGain_);
    }

    /// Set the decay parameter controlling Q-factor.
    /// Maps to Q range [100, 1000]. Affects newly added resonators only.
    /// @param decay  Normalized decay [0.0, 1.0]
    void setDecay(float decay) noexcept {
        // Logarithmic mapping: 0.0 -> Q=100, 1.0 -> Q=1000
        userQ_ = 100.0f * std::pow(10.0f, decay);
    }

    // =========================================================================
    // Voice Events (called from MIDI handlers, on audio thread)
    // =========================================================================

    /// Add resonators for a new voice's partials.
    /// Merges with existing resonators when frequencies are within ~0.3 Hz.
    /// True duplicates (same voiceId re-triggered) always merge.
    /// @param voiceId   Unique voice identifier
    /// @param partials  Inharmonicity-adjusted partial frequencies
    void noteOn(int32_t voiceId, const SympatheticPartialInfo& partials) noexcept {
        // FR-011: For same voiceId re-trigger, orphan existing resonators first
        noteOff(voiceId);

        for (int p = 0; p < kSympatheticPartialCount; ++p) {
            float freq = partials.frequencies[static_cast<size_t>(p)];

            // Skip invalid frequencies
            if (freq <= 0.0f || freq >= sampleRate_ * 0.5f) {
                continue;
            }

            int partialNumber = p + 1; // 1-based

            // Check for merge candidate
            int mergeIdx = findMergeCandidate(freq);
            if (mergeIdx >= 0) {
                auto midx = static_cast<size_t>(mergeIdx);
                // Merge: weighted average frequency
                // Use refCount as weight for the existing frequency
                int rc = refCounts_[midx];
                float existingFreq = freqs_[midx];
                float newFreq = (existingFreq * static_cast<float>(rc) + freq)
                                / static_cast<float>(rc + 1);
                freqs_[midx] = newFreq;
                refCounts_[midx] = rc + 1;

                // Track this voice as a co-owner by adding to ownerVoiceIds
                for (int ov = 0; ov < kMaxOwnersPerResonator; ++ov) {
                    if (ownerVoiceIds_[midx][static_cast<size_t>(ov)] == -1) {
                        ownerVoiceIds_[midx][static_cast<size_t>(ov)] = voiceId;
                        break;
                    }
                }

                // Recompute coefficients for updated frequency
                float Q_eff = computeFreqDependentQ(userQ_, newFreq);
                auto coeffs = computeResonatorCoeffs(newFreq, Q_eff, sampleRate_);
                coeffs_[midx] = coeffs.coeff;
                rSquareds_[midx] = coeffs.rSquared;
            } else {
                // Acquire new slot
                int slot = findFreeSlot();
                if (slot < 0) {
                    // Pool is full -- evict quietest
                    slot = evictQuietest();
                }
                if (slot < 0) continue; // Should not happen

                auto idx = static_cast<size_t>(slot);

                float Q_eff = computeFreqDependentQ(userQ_, freq);
                auto coeffs = computeResonatorCoeffs(freq, Q_eff, sampleRate_);

                freqs_[idx] = freq;
                coeffs_[idx] = coeffs.coeff;
                rSquareds_[idx] = coeffs.rSquared;
                y1s_[idx] = 0.0f;
                y2s_[idx] = 0.0f;
                // Gain-normalized so peak resonant response is approximately unity
                // regardless of Q or frequency. The digital resonator's peak gain is:
                //   |H(ω₀)| = 1 / [(1-r) × √(1 - 2r⋅cos(2ω₀) + r²)]
                // We divide by this so the resonator contributes ≈1× at resonance.
                // Per-partial weighting (1/sqrt(n)) still applied on top.
                float peakGainInv = computeResonatorPeakGainInverse(coeffs, freq);
                gains_[idx] = peakGainInv / std::sqrt(static_cast<float>(partialNumber));
                envelopes_[idx] = 1.0f; // Start above reclaim threshold to prevent premature eviction
                voiceIds_[idx] = voiceId;
                partialNumbers_[idx] = partialNumber;
                refCounts_[idx] = 1;
                actives_[idx] = true;
                activeCount_++;

                // Initialize owner list with this voice
                ownerVoiceIds_[idx][0] = voiceId;
                for (int ov = 1; ov < kMaxOwnersPerResonator; ++ov) {
                    ownerVoiceIds_[idx][static_cast<size_t>(ov)] = -1;
                }
            }
        }
    }

    /// Mark a voice as released. Existing resonators continue to ring out.
    /// Resonators are reclaimed only when amplitude drops below -96 dB.
    /// @param voiceId   Voice identifier that was released
    void noteOff(int32_t voiceId) noexcept {
        for (int i = 0; i < kMaxSympatheticResonators; ++i) {
            auto idx = static_cast<size_t>(i);
            if (!actives_[idx]) continue;

            // Check if this voice is an owner of this resonator
            bool isOwner = false;
            for (int ov = 0; ov < kMaxOwnersPerResonator; ++ov) {
                if (ownerVoiceIds_[idx][static_cast<size_t>(ov)] == voiceId) {
                    // Remove this voice from owner list
                    ownerVoiceIds_[idx][static_cast<size_t>(ov)] = -1;
                    isOwner = true;
                    break;
                }
            }

            if (!isOwner) continue;

            refCounts_[idx]--;
            if (refCounts_[idx] <= 0) {
                // Last owner released -- orphan the resonator
                voiceIds_[idx] = -1;
                refCounts_[idx] = 0;
            } else {
                // Update voiceId to the next remaining owner
                voiceIds_[idx] = -1; // Default to orphaned
                for (int ov = 0; ov < kMaxOwnersPerResonator; ++ov) {
                    if (ownerVoiceIds_[idx][static_cast<size_t>(ov)] != -1) {
                        voiceIds_[idx] = ownerVoiceIds_[idx][static_cast<size_t>(ov)];
                        break;
                    }
                }
            }
        }
    }

    // =========================================================================
    // Processing (called per sample in the audio loop)
    // =========================================================================

    /// Process one sample of sympathetic resonance.
    /// Feeds the input (scaled by coupling amount) into all active resonators,
    /// sums their outputs, applies anti-mud HPF, and returns the result.
    ///
    /// When amount is 0.0, returns 0.0 with zero computation (bypassed).
    ///
    /// @param input  Mono input sample (global voice sum, post poly-gain comp)
    /// @return       Sympathetic output sample (to be added to master output)
    [[nodiscard]] float process(float input) noexcept {
        // FR-014: Early-out when bypassed
        if (isBypassed()) {
            return 0.0f;
        }

        // Advance smoother for this sample (FR-023)
        float smoothedAmount = amountSmoother_.process();

        // Scale input by smoothed coupling gain
        float scaledInput = input * smoothedAmount;

        float sum = 0.0f;

        // SIMD-accelerated resonator loop: processes ALL kMaxSympatheticResonators
        // slots. Inactive slots have coeffs=0, gains=0, y1s=0, y2s=0 so they
        // produce zero output and zero state change -- safe to process in bulk.
        processSympatheticBankSIMD(
            y1s_.data(), y2s_.data(),
            coeffs_.data(), rSquareds_.data(), gains_.data(),
            kMaxSympatheticResonators, scaledInput, &sum,
            envelopeReleaseCoeff_, envelopes_.data());

        // Scalar reclaim pass: check envelopes and reclaim dead resonators.
        // This runs after SIMD to avoid branching inside the vectorized loop.
        for (int i = 0; i < kMaxSympatheticResonators; ++i) {
            auto idx = static_cast<size_t>(i);
            if (!actives_[idx]) continue;

            if (envelopes_[idx] < kReclaimThresholdLinear) {
                actives_[idx] = false;
                y1s_[idx] = 0.0f;
                y2s_[idx] = 0.0f;
                envelopes_[idx] = 0.0f;
                gains_[idx] = 0.0f;
                coeffs_[idx] = 0.0f;
                rSquareds_[idx] = 0.0f;
                ownerVoiceIds_[idx].fill(-1);
                activeCount_--;
            }
        }

        // Apply anti-mud HPF to summed output (FR-012)
        float output = antiMudHpf_.process(sum);

        return output;
    }

    // =========================================================================
    // Query (for testing and diagnostics)
    // =========================================================================

    /// @return Number of currently active resonators in the pool.
    [[nodiscard]] int getActiveResonatorCount() const noexcept {
        return activeCount_;
    }

    /// @return Whether the component is completely bypassed (amount == 0.0).
    [[nodiscard]] bool isBypassed() const noexcept {
        return amountSmoother_.getCurrentValue() == 0.0f && couplingGain_ == 0.0f;
    }

    /// Get the frequency of the resonator at the given pool index (for testing).
    /// Returns 0.0f if the slot is inactive or index is out of range.
    [[nodiscard]] float getResonatorFrequency(int index) const noexcept {
        if (index < 0 || index >= kMaxSympatheticResonators) return 0.0f;
        auto idx = static_cast<size_t>(index);
        if (!actives_[idx]) return 0.0f;
        return freqs_[idx];
    }

private:
    // =========================================================================
    // Internal Types
    // =========================================================================

    struct ResonatorCoeffs {
        float coeff;    // 2*r*cos(omega)
        float rSquared; // r*r
    };

    // =========================================================================
    // Private Helpers
    // =========================================================================

    /// Compute the inverse of the resonator's peak gain at its resonant frequency.
    /// For a driven second-order resonator y[n] = coeff*y1 - r²*y2 + x[n],
    /// the peak gain at ω₀ is: 1 / [(1-r) × √(1 - 2r⋅cos(2ω₀) + r²)]
    /// Returns the reciprocal so it can be used directly as a gain multiplier.
    [[nodiscard]] static float computeResonatorPeakGainInverse(
        const ResonatorCoeffs& c, float freq
    ) noexcept {
        // Recover r from r²
        float r = std::sqrt(c.rSquared);
        float oneMinusR = 1.0f - r;
        if (oneMinusR < 1e-12f) return 1e-6f; // Degenerate: r≈1

        // cos(2ω) from cos(ω): use double-angle identity
        // coeff = 2*r*cos(ω), so cos(ω) = coeff / (2*r)
        float cosOmega = (r > 1e-12f) ? c.coeff / (2.0f * r) : 1.0f;
        float cos2Omega = 2.0f * cosOmega * cosOmega - 1.0f;

        // |D|² = (1-r)² × (1 - 2r⋅cos(2ω) + r²)
        float inner = 1.0f - 2.0f * r * cos2Omega + c.rSquared;
        if (inner < 1e-12f) inner = 1e-12f;
        float sqrtInner = std::sqrt(inner);

        return oneMinusR * sqrtInner;
    }

    /// Compute resonator coefficients from frequency, Q, and sample rate.
    /// r = exp(-pi * (f/Q) / sampleRate)
    /// omega = 2*pi*f / sampleRate
    /// coeff = 2*r*cos(omega)
    [[nodiscard]] static ResonatorCoeffs computeResonatorCoeffs(
        float f, float Q_eff, float sampleRate
    ) noexcept {
        float delta_f = f / Q_eff;
        float r = std::exp(-kPi * delta_f / sampleRate);
        float omega = kTwoPi * f / sampleRate;
        return ResonatorCoeffs{
            2.0f * r * std::cos(omega),
            r * r
        };
    }

    /// Compute frequency-dependent Q.
    /// Q_eff = Q_user * clamp(kQFreqRef / f, kMinQScale, 1.0)
    [[nodiscard]] static float computeFreqDependentQ(float Q_user, float f) noexcept {
        float scale = kQFreqRef / f;
        scale = std::clamp(scale, kMinQScale, 1.0f);
        return Q_user * scale;
    }

    /// Find a merge candidate in the pool for the given frequency.
    /// @return Index of merge candidate, or -1 if none found.
    [[nodiscard]] int findMergeCandidate(float freq) const noexcept {
        for (int i = 0; i < kMaxSympatheticResonators; ++i) {
            auto idx = static_cast<size_t>(i);
            if (actives_[idx]) {
                if (std::abs(freqs_[idx] - freq) < kMergeThresholdHz) {
                    return i;
                }
            }
        }
        return -1;
    }

    /// Find a free (inactive) slot in the pool.
    /// @return Index of free slot, or -1 if pool is full.
    [[nodiscard]] int findFreeSlot() const noexcept {
        for (int i = 0; i < kMaxSympatheticResonators; ++i) {
            if (!actives_[static_cast<size_t>(i)]) {
                return i;
            }
        }
        return -1;
    }

    /// Evict the quietest active resonator and return its slot index.
    /// @return Index of evicted slot.
    [[nodiscard]] int evictQuietest() noexcept {
        int quietestIdx = -1;
        float quietestEnv = 1e30f;
        for (int i = 0; i < kMaxSympatheticResonators; ++i) {
            auto idx = static_cast<size_t>(i);
            if (actives_[idx] && envelopes_[idx] < quietestEnv) {
                quietestEnv = envelopes_[idx];
                quietestIdx = i;
            }
        }
        if (quietestIdx >= 0) {
            auto idx = static_cast<size_t>(quietestIdx);
            actives_[idx] = false;
            y1s_[idx] = 0.0f;
            y2s_[idx] = 0.0f;
            envelopes_[idx] = 0.0f;
            coeffs_[idx] = 0.0f;
            rSquareds_[idx] = 0.0f;
            gains_[idx] = 0.0f;
            ownerVoiceIds_[idx].fill(-1);
            activeCount_--;
        }
        return quietestIdx;
    }

    /// Reset the entire resonator pool to inactive.
    void resetPool() noexcept {
        freqs_.fill(0.0f);
        coeffs_.fill(0.0f);
        rSquareds_.fill(0.0f);
        y1s_.fill(0.0f);
        y2s_.fill(0.0f);
        gains_.fill(0.0f);
        envelopes_.fill(0.0f);
        voiceIds_.fill(-1);
        partialNumbers_.fill(0);
        refCounts_.fill(0);
        actives_.fill(false);
        activeCount_ = 0;

        // Initialize all owner voice ID slots to -1
        for (auto& owners : ownerVoiceIds_) {
            owners.fill(-1);
        }
    }

    // =========================================================================
    // SoA Pool Arrays (SIMD-ready layout)
    // =========================================================================

    std::array<float, kMaxSympatheticResonators> freqs_{};
    std::array<float, kMaxSympatheticResonators> coeffs_{};
    std::array<float, kMaxSympatheticResonators> rSquareds_{};
    std::array<float, kMaxSympatheticResonators> y1s_{};
    std::array<float, kMaxSympatheticResonators> y2s_{};
    std::array<float, kMaxSympatheticResonators> gains_{};
    std::array<float, kMaxSympatheticResonators> envelopes_{};
    std::array<int32_t, kMaxSympatheticResonators> voiceIds_{};
    std::array<int, kMaxSympatheticResonators> partialNumbers_{};
    std::array<int, kMaxSympatheticResonators> refCounts_{};
    std::array<std::array<int32_t, kMaxOwnersPerResonator>, kMaxSympatheticResonators> ownerVoiceIds_{};
    std::array<bool, kMaxSympatheticResonators> actives_{};

    // =========================================================================
    // Members
    // =========================================================================

    int activeCount_ = 0;
    float sampleRate_ = 44100.0f;
    float userQ_ = 316.2f;
    float couplingGain_ = 0.0f;
    float envelopeReleaseCoeff_ = 0.0f;

    Biquad antiMudHpf_;
    OnePoleSmoother amountSmoother_;
};

}  // namespace DSP
}  // namespace Krate
