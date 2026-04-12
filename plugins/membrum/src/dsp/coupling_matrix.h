#pragma once

// ==============================================================================
// CouplingMatrix -- Phase 5 two-layer coupling coefficient resolver
// ==============================================================================
// FR-030: Two-layer resolver: {computedGain, overrideGain, hasOverride}
// FR-031: Per-pair coefficients clamped to [0.0, 0.05]
// FR-034: Tier 1 recomputation on knob change
// ==============================================================================

#include "pad_category.h"
#include "pad_config.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace Membrum {

class CouplingMatrix {
public:
    static constexpr float kMaxCoefficient = 0.05f;
    static constexpr int kSize = kNumPads; // 32

    CouplingMatrix() noexcept = default;

    // =========================================================================
    // Tier 1: Recompute computed gains from global knobs + categories
    // =========================================================================

    /// Recompute all computedGain values and resolve into effectiveGain.
    /// Called when Snare Buzz, Tom Resonance, or any pad config changes.
    ///
    /// Overload without per-pad amounts -- treats all pads as amount = 1.0.
    /// Used for Phase 5 initial wiring and by tests that don't exercise
    /// Phase 6 per-pad participation.
    void recomputeFromTier1(float snareBuzz,
                            float tomResonance,
                            const PadCategory* categories) noexcept
    {
        recomputeFromTier1(snareBuzz, tomResonance, categories, nullptr);
    }

    /// Phase 6 (US4 / FR-014): per-pad coupling amount baked into the
    /// effectiveGain formula. `padCouplingAmounts` is a kSize-element array
    /// in [0.0, 1.0]; may be nullptr to skip the multiplication (all pads
    /// treated as 1.0). When provided, each pair's computed gain is scaled
    /// by `amounts[src] * amounts[dst]` so that a pad with amount = 0 is
    /// completely excluded as both source and receiver (FR-023).
    void recomputeFromTier1(float snareBuzz,
                            float tomResonance,
                            const PadCategory* categories,
                            const float* padCouplingAmounts) noexcept
    {
        for (int src = 0; src < kSize; ++src) {
            for (int dst = 0; dst < kSize; ++dst) {
                if (src == dst) {
                    computedGain_[src][dst] = 0.0f;
                    continue;
                }

                float gain = 0.0f;
                // Kick -> Snare: snare buzz path
                if (categories[src] == PadCategory::Kick &&
                    categories[dst] == PadCategory::Snare) {
                    gain = snareBuzz * kMaxCoefficient;
                }
                // Tom -> Tom: tom resonance path
                else if (categories[src] == PadCategory::Tom &&
                         categories[dst] == PadCategory::Tom) {
                    gain = tomResonance * kMaxCoefficient;
                }

                if (padCouplingAmounts != nullptr) {
                    const float aSrc = std::clamp(padCouplingAmounts[src],
                                                  0.0f, 1.0f);
                    const float aDst = std::clamp(padCouplingAmounts[dst],
                                                  0.0f, 1.0f);
                    gain *= aSrc * aDst;
                }

                computedGain_[src][dst] = std::clamp(gain, 0.0f, kMaxCoefficient);
            }
        }
        resolveAll();
    }

    // =========================================================================
    // Tier 2: Per-pair overrides (Phase 6 UI / programmatic)
    // =========================================================================

    void setOverride(int src, int dst, float coeff) noexcept
    {
        if (src < 0 || src >= kSize || dst < 0 || dst >= kSize) return;
        overrideGain_[src][dst] = std::clamp(coeff, 0.0f, kMaxCoefficient);
        hasOverride_[src][dst] = true;
        resolve(src, dst);
    }

    void clearOverride(int src, int dst) noexcept
    {
        if (src < 0 || src >= kSize || dst < 0 || dst >= kSize) return;
        hasOverride_[src][dst] = false;
        resolve(src, dst);
    }

    [[nodiscard]] bool hasOverrideAt(int src, int dst) const noexcept
    {
        if (src < 0 || src >= kSize || dst < 0 || dst >= kSize) return false;
        return hasOverride_[src][dst];
    }

    [[nodiscard]] float getOverrideGain(int src, int dst) const noexcept
    {
        if (src < 0 || src >= kSize || dst < 0 || dst >= kSize) return 0.0f;
        return overrideGain_[src][dst];
    }

    // =========================================================================
    // Resolved access (audio thread)
    // =========================================================================

    [[nodiscard]] float getEffectiveGain(int src, int dst) const noexcept
    {
        if (src < 0 || src >= kSize || dst < 0 || dst >= kSize) return 0.0f;
        return effectiveGain_[src][dst];
    }

    [[nodiscard]] const float (&effectiveGainArray() const noexcept)[kSize][kSize]
    {
        return effectiveGain_;
    }

    // =========================================================================
    // Serialization helpers
    // =========================================================================

    /// Count pairs with active overrides.
    [[nodiscard]] int getOverrideCount() const noexcept
    {
        int count = 0;
        for (int s = 0; s < kSize; ++s)
            for (int d = 0; d < kSize; ++d)
                if (hasOverride_[s][d]) ++count;
        return count;
    }

    /// Iterate all overrides. Fn signature: void(int src, int dst, float coeff).
    template <typename Fn>
    void forEachOverride(Fn&& fn) const noexcept
    {
        for (int s = 0; s < kSize; ++s)
            for (int d = 0; d < kSize; ++d)
                if (hasOverride_[s][d])
                    fn(s, d, overrideGain_[s][d]);
    }

    /// Clear all overrides and computed gains (for state load).
    void clearAll() noexcept
    {
        for (int s = 0; s < kSize; ++s)
            for (int d = 0; d < kSize; ++d) {
                computedGain_[s][d] = 0.0f;
                overrideGain_[s][d] = 0.0f;
                hasOverride_[s][d] = false;
                effectiveGain_[s][d] = 0.0f;
            }
    }

private:
    void resolve(int src, int dst) noexcept
    {
        effectiveGain_[src][dst] = hasOverride_[src][dst]
            ? overrideGain_[src][dst]
            : computedGain_[src][dst];
    }

    void resolveAll() noexcept
    {
        for (int s = 0; s < kSize; ++s)
            for (int d = 0; d < kSize; ++d)
                resolve(s, d);
    }

    float computedGain_[kSize][kSize]{};
    float overrideGain_[kSize][kSize]{};
    bool  hasOverride_[kSize][kSize]{};
    float effectiveGain_[kSize][kSize]{};
};

} // namespace Membrum
