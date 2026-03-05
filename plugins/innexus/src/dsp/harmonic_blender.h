// ==============================================================================
// HarmonicBlender -- Multi-Source Spectral Blending
// ==============================================================================
// Plugin-local DSP class for Innexus M6 Phase 21.
// Location: plugins/innexus/src/dsp/harmonic_blender.h
//
// Spec: specs/120-creative-extensions/spec.md
// Covers: FR-034 to FR-042
// ==============================================================================

#pragma once

#include <krate/dsp/processors/harmonic_frame_utils.h>
#include <krate/dsp/processors/harmonic_snapshot.h>
#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/processors/residual_types.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace Innexus {

/// @brief Multi-source harmonic blending from weighted memory slots (FR-034).
///
/// Accepts up to 8 stored snapshots plus 1 optional live analysis frame,
/// each with an independent weight. Weights are normalized internally
/// before blending (FR-035). Empty slots contribute zero regardless of weight.
///
/// The blended model is a weighted sum of spectral data (FR-037):
///   blendedAmp_n = sum(weight_i * sourceAmp_n_i)
///   blendedRelativeFreq_n = sum(weight_i * sourceRelFreq_n_i)
///   blendedResidualBands_k = sum(weight_i * sourceResidualBands_k_i)
///
/// Component-matching: missing partials (beyond a source's numPartials)
/// contribute zero amplitude (FR-038).
///
/// @par Thread Safety: Single-threaded (audio thread only).
/// @par Real-Time Safety: All methods noexcept, no allocations.
class HarmonicBlender {
public:
    /// Number of memory slot sources.
    static constexpr int kNumSlots = 8;

    HarmonicBlender() noexcept = default;

    /// @brief Set weight for a memory slot source (FR-035).
    /// @param slotIndex Slot index [0, 7]
    /// @param weight Raw weight [0.0, 1.0]
    void setSlotWeight(int slotIndex, float weight) noexcept
    {
        if (slotIndex >= 0 && slotIndex < kNumSlots)
            slotWeights_[static_cast<size_t>(slotIndex)] = weight;
    }

    /// @brief Set weight for the live analysis source (FR-036).
    /// @param weight Raw weight [0.0, 1.0]
    void setLiveWeight(float weight) noexcept
    {
        liveWeight_ = weight;
    }

    /// @brief Compute blended output from weighted sources (FR-037).
    ///
    /// Normalizes weights internally (FR-035). If all weights are zero,
    /// outputs silence (FR-039).
    ///
    /// @param slots Array of 8 MemorySlot references (for snapshot data)
    /// @param liveFrame Current live analysis harmonic frame (may be empty)
    /// @param liveResidual Current live analysis residual frame (may be empty)
    /// @param hasLiveSource true if live analysis is active
    /// @param[out] frame Blended harmonic frame
    /// @param[out] residual Blended residual frame
    /// @return true if valid output produced (at least one nonzero weight on occupied source)
    [[nodiscard]] bool blend(
        const std::array<Krate::DSP::MemorySlot, 8>& slots,
        const Krate::DSP::HarmonicFrame& liveFrame,
        const Krate::DSP::ResidualFrame& liveResidual,
        bool hasLiveSource,
        Krate::DSP::HarmonicFrame& frame,
        Krate::DSP::ResidualFrame& residual) const noexcept
    {
        // Zero outputs
        frame = Krate::DSP::HarmonicFrame{};
        residual = Krate::DSP::ResidualFrame{};

        // Compute total weight for normalization (R-006).
        // Only count occupied slots and (if active) live source.
        float totalWeight = 0.0f;
        for (int i = 0; i < kNumSlots; ++i)
        {
            if (slots[static_cast<size_t>(i)].occupied && slotWeights_[static_cast<size_t>(i)] > 0.0f)
                totalWeight += slotWeights_[static_cast<size_t>(i)];
        }
        if (hasLiveSource && liveWeight_ > 0.0f)
            totalWeight += liveWeight_;

        // Cache normalized weights for getEffective* queries
        cachedTotalWeight_ = totalWeight;
        cachedHasLiveSource_ = hasLiveSource;
        cachedEffectiveSlotWeights_.fill(0.0f);
        cachedEffectiveLiveWeight_ = 0.0f;

        // FR-039: all-zero weights -> silence
        if (totalWeight <= 0.0f)
            return false;

        const float invTotal = 1.0f / totalWeight;

        // Pre-compute effective weights for getEffective* queries
        for (int i = 0; i < kNumSlots; ++i)
        {
            if (slots[static_cast<size_t>(i)].occupied && slotWeights_[static_cast<size_t>(i)] > 0.0f)
                cachedEffectiveSlotWeights_[static_cast<size_t>(i)] =
                    slotWeights_[static_cast<size_t>(i)] * invTotal;
        }
        if (hasLiveSource && liveWeight_ > 0.0f)
            cachedEffectiveLiveWeight_ = liveWeight_ * invTotal;

        // Determine max partial count across all contributing sources
        int maxPartials = 0;

        // Blend each contributing slot
        for (int slotIdx = 0; slotIdx < kNumSlots; ++slotIdx)
        {
            const auto& slot = slots[static_cast<size_t>(slotIdx)];
            if (!slot.occupied || slotWeights_[static_cast<size_t>(slotIdx)] <= 0.0f)
                continue;

            const float w = slotWeights_[static_cast<size_t>(slotIdx)] * invTotal;
            const auto& snap = slot.snapshot;

            // Recall snapshot to frame for blending
            // We blend directly from snapshot data to avoid allocating temporary frames
            const int np = std::min(snap.numPartials, static_cast<int>(Krate::DSP::kMaxPartials));
            maxPartials = std::max(maxPartials, np);

            for (int p = 0; p < np; ++p)
            {
                auto& rp = frame.partials[static_cast<size_t>(p)];
                rp.amplitude += w * snap.normalizedAmps[static_cast<size_t>(p)];
                rp.relativeFrequency += w * snap.relativeFreqs[static_cast<size_t>(p)];
                rp.inharmonicDeviation += w * snap.inharmonicDeviation[static_cast<size_t>(p)];
            }

            // Blend residual bands
            for (size_t b = 0; b < Krate::DSP::kResidualBands; ++b)
                residual.bandEnergies[b] += w * snap.residualBands[b];
            residual.totalEnergy += w * snap.residualEnergy;

            // Blend metadata
            frame.f0 += w * snap.f0Reference;
            frame.globalAmplitude += w * snap.globalAmplitude;
            frame.spectralCentroid += w * snap.spectralCentroid;
            frame.brightness += w * snap.brightness;
        }

        // Blend live source if active
        if (hasLiveSource && liveWeight_ > 0.0f)
        {
            const float w = liveWeight_ * invTotal;
            const int np = std::min(liveFrame.numPartials, static_cast<int>(Krate::DSP::kMaxPartials));
            maxPartials = std::max(maxPartials, np);

            for (int p = 0; p < np; ++p)
            {
                auto& rp = frame.partials[static_cast<size_t>(p)];
                rp.amplitude += w * liveFrame.partials[static_cast<size_t>(p)].amplitude;
                rp.relativeFrequency += w * liveFrame.partials[static_cast<size_t>(p)].relativeFrequency;
                rp.inharmonicDeviation += w * liveFrame.partials[static_cast<size_t>(p)].inharmonicDeviation;
            }

            for (size_t b = 0; b < Krate::DSP::kResidualBands; ++b)
                residual.bandEnergies[b] += w * liveResidual.bandEnergies[b];
            residual.totalEnergy += w * liveResidual.totalEnergy;

            frame.f0 += w * liveFrame.f0;
            frame.globalAmplitude += w * liveFrame.globalAmplitude;
            frame.spectralCentroid += w * liveFrame.spectralCentroid;
            frame.brightness += w * liveFrame.brightness;
        }

        frame.numPartials = maxPartials;
        frame.f0Confidence = 1.0f;

        // Fill in harmonicIndex and other per-partial metadata
        for (int p = 0; p < maxPartials; ++p)
        {
            auto& rp = frame.partials[static_cast<size_t>(p)];
            rp.harmonicIndex = p + 1;
            rp.stability = 1.0f;
            rp.age = 1;
            // Compute frequency from relative (using blended f0)
            rp.frequency = rp.relativeFrequency * frame.f0;
        }

        return true;
    }

    /// @brief Get normalized effective weight for a slot.
    /// @param slotIndex Slot index [0, 7]
    /// @return Normalized weight (after dividing by total)
    [[nodiscard]] float getEffectiveSlotWeight(int slotIndex) const noexcept
    {
        if (slotIndex < 0 || slotIndex >= kNumSlots)
            return 0.0f;
        return cachedEffectiveSlotWeights_[static_cast<size_t>(slotIndex)];
    }

    /// @brief Get normalized effective weight for the live source.
    [[nodiscard]] float getEffectiveLiveWeight() const noexcept
    {
        return cachedEffectiveLiveWeight_;
    }

private:
    std::array<float, kNumSlots> slotWeights_{};
    float liveWeight_ = 0.0f;

    // Cached after last blend() call for getEffective* queries
    mutable float cachedTotalWeight_ = 0.0f;
    mutable bool cachedHasLiveSource_ = false;
    mutable std::array<float, kNumSlots> cachedEffectiveSlotWeights_{};
    mutable float cachedEffectiveLiveWeight_ = 0.0f;
};

} // namespace Innexus
