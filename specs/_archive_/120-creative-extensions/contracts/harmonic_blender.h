// ==============================================================================
// CONTRACT: HarmonicBlender -- Multi-Source Spectral Blending
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

#include <array>

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
    void setSlotWeight(int slotIndex, float weight) noexcept;

    /// @brief Set weight for the live analysis source (FR-036).
    /// @param weight Raw weight [0.0, 1.0]
    void setLiveWeight(float weight) noexcept;

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
        Krate::DSP::ResidualFrame& residual) const noexcept;

    /// @brief Get normalized effective weight for a slot.
    /// @param slotIndex Slot index [0, 7]
    /// @return Normalized weight (after dividing by total)
    [[nodiscard]] float getEffectiveSlotWeight(int slotIndex) const noexcept;

    /// @brief Get normalized effective weight for the live source.
    [[nodiscard]] float getEffectiveLiveWeight() const noexcept;

private:
    std::array<float, kNumSlots> slotWeights_{};
    float liveWeight_ = 0.0f;
};

} // namespace Innexus
