#pragma once

// ==============================================================================
// CouplingMatrix -- single-tier coupling coefficient resolver
// ==============================================================================
// Resolves per-pair sympathetic-resonance coefficients from the global
// (Snare Buzz, Tom Resonance) knobs and per-pad coupling amounts. The
// previous Tier 2 per-pair override layer + Solo were removed when the
// 32x32 Matrix UI was retired -- nothing produced overrides anymore.
// Per-pair coefficients clamp to [0.0, kMaxCoefficient].
// ==============================================================================

#include "pad_category.h"
#include "pad_config.h"

#include <algorithm>
#include <array>

namespace Membrum {

class CouplingMatrix {
public:
    static constexpr float kMaxCoefficient = 0.05f;
    static constexpr int kSize = kNumPads; // 32

    CouplingMatrix() noexcept = default;

    /// Recompute every (src, dst) pair from the global knobs + categories.
    /// Overload without per-pad amounts -- treats all pads as amount = 1.0.
    void recomputeFromTier1(float snareBuzz,
                            float tomResonance,
                            const PadCategory* categories) noexcept
    {
        recomputeFromTier1(snareBuzz, tomResonance, categories, nullptr);
    }

    /// Per-pad coupling amount baked into the gain formula. `padCouplingAmounts`
    /// is a kSize-element array in [0.0, 1.0]; may be nullptr to skip the
    /// multiplication (all pads treated as 1.0). When provided, each pair's
    /// gain is scaled by `amounts[src] * amounts[dst]` so a pad with amount = 0
    /// is excluded as both source and receiver.
    void recomputeFromTier1(float snareBuzz,
                            float tomResonance,
                            const PadCategory* categories,
                            const float* padCouplingAmounts) noexcept
    {
        for (int src = 0; src < kSize; ++src) {
            for (int dst = 0; dst < kSize; ++dst) {
                if (src == dst) {
                    effectiveGain_[src][dst] = 0.0f;
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

                effectiveGain_[src][dst] = std::clamp(gain, 0.0f, kMaxCoefficient);
            }
        }
    }

    [[nodiscard]] float getEffectiveGain(int src, int dst) const noexcept
    {
        if (src < 0 || src >= kSize || dst < 0 || dst >= kSize) return 0.0f;
        return effectiveGain_[src][dst];
    }

    [[nodiscard]] const float (&effectiveGainArray() const noexcept)[kSize][kSize]
    {
        return effectiveGain_;
    }

    /// Reset every cell to zero (used during state load).
    void clearAll() noexcept
    {
        for (int s = 0; s < kSize; ++s)
            for (int d = 0; d < kSize; ++d)
                effectiveGain_[s][d] = 0.0f;
    }

private:
    float effectiveGain_[kSize][kSize]{};
};

} // namespace Membrum
