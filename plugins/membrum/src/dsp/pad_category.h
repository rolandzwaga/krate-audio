#pragma once

// ==============================================================================
// PadCategory -- Phase 5 pad classification for coupling matrix (FR-033)
// ==============================================================================
// Priority-ordered rule chain for deriving pad category from runtime config.
// Used by CouplingMatrix::recomputeFromTier1 to map Tier 1 knobs to pairs.
// ==============================================================================

#include "pad_config.h"
#include "exciter_type.h"
#include "body_model_type.h"

namespace Membrum {

enum class PadCategory : int {
    Kick,       // Membrane body + pitch envelope active (tsPitchEnvTime > 0)
    Snare,      // Membrane body + NoiseBurst exciter
    Tom,        // Membrane body (no pitch env, no noise exciter)
    HatCymbal,  // NoiseBody
    Perc,       // Any other configuration
    kCount
};

/// Derive pad category from runtime PadConfig.
/// Priority-ordered: first matching rule wins (FR-033).
[[nodiscard]] inline PadCategory classifyPad(const PadConfig& cfg) noexcept
{
    if (cfg.bodyModel == BodyModelType::Membrane) {
        // Rule 1: Membrane + pitch envelope active -> Kick
        if (cfg.tsPitchEnvTime > 0.0f)
            return PadCategory::Kick;
        // Rule 2: Membrane + noise exciter -> Snare
        if (cfg.exciterType == ExciterType::NoiseBurst)
            return PadCategory::Snare;
        // Rule 3: Membrane only -> Tom
        return PadCategory::Tom;
    }
    // Rule 4: NoiseBody -> HatCymbal
    if (cfg.bodyModel == BodyModelType::NoiseBody)
        return PadCategory::HatCymbal;
    // Rule 5: Everything else -> Perc
    return PadCategory::Perc;
}

} // namespace Membrum
