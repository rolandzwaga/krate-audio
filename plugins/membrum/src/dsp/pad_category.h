#pragma once

// ==============================================================================
// PadCategory -- Phase 5 pad classification for coupling matrix (FR-033)
// ==============================================================================
// Priority-ordered rule chain for deriving pad category from runtime config.
// Used by CouplingMatrix::recomputeFromTier1 to map Tier 1 knobs to pairs
// (Kick -> Snare carries Snare Buzz, Tom -> Tom carries Tom Resonance) and by
// the pad grid's K/S/T/H/P glyph.
//
// Fix plan T3.3 (MB-183): a pad is classified by what it is, not by its
// exciter. A kick is a low drum whose pitch drops onto a low end pitch; a snare
// is a drum with wires (the redesigned snares are Impulse + the noise layer as
// wires, the older recipe is a NoiseBurst strike on a mid-pitched head).
// ==============================================================================

#include "pad_config.h"
#include "exciter_type.h"
#include "body_model_type.h"
#include <cstdint>

namespace Membrum {

enum class PadCategory : std::uint8_t {
    Kick,       // Membrane + pitch envelope settling at or below 90 Hz (a Mallet
                // pad only with a kick's short, <= 50 ms, drop)
    Snare,      // Membrane + snare wires: noise layer lifted above its accent
                // level, wire coupling on, or a NoiseBurst strike that settles
                // at or below 180 Hz (or has no pitch envelope)
    Tom,        // Any other Membrane pad (toms, congas, bongos, timbales, ...)
    HatCymbal,  // NoiseBody
    Perc,       // Any other configuration
    kCount
};

/// Kick end pitch: the pitch envelope's end is stored as Hz = 20 * 100^n
/// (pad_config_apply.h, setPitchEnvEndHz), so 90 Hz is
/// n = log(90 / 20) / log(100) = log10(4.5) / 2 = 0.3266063. Factory kicks end
/// at 30-85 Hz; snares at 110-170 Hz, toms and hand drums mostly higher.
inline constexpr float kKickEndNorm = 0.3266063f;

/// A Mallet pad is a kick only when its pitch drop is a kick's thump: at most
/// 50 ms (tsPitchEnvTime is normalised over 500 ms, so 0.1). Factory Mallet
/// kicks drop in 22-50 ms; the Mallet toms' downward "tonk" glide (tom-depth
/// fix: default kit 120-250 ms) runs longer, often onto a floor-tom pitch
/// below 90 Hz.
inline constexpr float kKickMaxMalletGlideTime = 0.1f;

/// NoiseBurst snare end pitch: 180 Hz, n = log(180 / 20) / log(100) =
/// log10(9) / 2 = 0.4771213. The factory NoiseBurst snares settle at 110-170 Hz;
/// the Latin Perc timbales (NoiseBurst Membrane, no wires) at 200 and 280 Hz.
inline constexpr float kSnareMaxEndNorm = 0.4771213f;

/// Derive pad category from runtime PadConfig.
/// Priority-ordered: first matching rule wins (FR-033).
[[nodiscard]] inline PadCategory classifyPad(const PadConfig& cfg) noexcept
{
    if (cfg.bodyModel == BodyModelType::Membrane) {
        const bool pitchEnv = cfg.tsPitchEnvTime > 0.0f;
        const bool mallet = cfg.exciterType == ExciterType::Mallet;
        // Rule 1: Kick -- the pitch envelope settles at or below 90 Hz. A
        // Mallet pad needs a kick's short drop; a longer glide is a tom's.
        if (pitchEnv && cfg.tsPitchEnvEnd <= kKickEndNorm &&
            (!mallet || cfg.tsPitchEnvTime <= kKickMaxMalletGlideTime))
            return PadCategory::Kick;
        // Rule 2: any other Mallet Membrane pad is a Tom (the tom "tonk"
        // glide, tom-depth fix).
        if (mallet)
            return PadCategory::Tom;
        // Rule 3: Snare -- wire evidence. The noise layer lifted above its
        // accent level or coupled to the head is the snare wire
        // (pad_config.h, noiseLayerGain / wireCoupling); a NoiseBurst strike
        // on a head that settles at or below 180 Hz is the older snare recipe.
        if (cfg.noiseLayerGain > 1.0f || cfg.wireCoupling > 0.0f)
            return PadCategory::Snare;
        if (cfg.exciterType == ExciterType::NoiseBurst &&
            (!pitchEnv || cfg.tsPitchEnvEnd <= kSnareMaxEndNorm))
            return PadCategory::Snare;
        // Rule 4: any other Membrane pad -> Tom
        return PadCategory::Tom;
    }
    // Rule 5: NoiseBody -> HatCymbal
    if (cfg.bodyModel == BodyModelType::NoiseBody)
        return PadCategory::HatCymbal;
    // Rule 6: Everything else -> Perc
    return PadCategory::Perc;
}

} // namespace Membrum
