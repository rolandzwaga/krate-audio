#pragma once

// ==============================================================================
// naturalFundamentalHz -- body-type-aware f0 formula
// ==============================================================================
// Membrum assigns each body type its own Size->f0 curve (FR-032):
//
//   Membrane:  500 Hz * 0.1^size   (50 .. 500 Hz)
//   Plate:     800 Hz * 0.1^size   (80 .. 800 Hz)
//   Shell:    1500 Hz * 0.1^size   (150 .. 1500 Hz)
//   String:    800 Hz * 0.1^size   (80 .. 800 Hz)
//   Bell:      800 Hz * 0.1^size   (80 .. 800 Hz, nominal partial)
//   NoiseBody:1500 Hz * 0.1^size   (150 .. 1500 Hz)
//
// The mapper files (membrane_mapper.h, plate_mapper.h, etc.) are the source of
// truth for these constants; this helper mirrors them so that downstream
// consumers (ToneShaper's pitch-envelope baseline, ModeInject's harmonic
// fundamental, Phase 8E tension modulation) can ask for the f0 that
// corresponds to the body model they're actually driving without duplicating
// the formula at each call site.
// ==============================================================================

#include "dsp/body_model_type.h"

#include <algorithm>
#include <cmath>

namespace Membrum::Bodies {

[[nodiscard]] inline float naturalFundamentalHz(BodyModelType type,
                                                float          size) noexcept
{
    const float clamped   = std::clamp(size, 0.0f, 1.0f);
    const float sizeDecay = std::pow(0.1f, clamped);

    switch (type)
    {
        case BodyModelType::Membrane:  return  500.0f * sizeDecay;
        case BodyModelType::Plate:     return  800.0f * sizeDecay;
        case BodyModelType::Shell:     return 1500.0f * sizeDecay;
        case BodyModelType::String:    return  800.0f * sizeDecay;
        case BodyModelType::Bell:      return  800.0f * sizeDecay;
        case BodyModelType::NoiseBody: return 1500.0f * sizeDecay;
        case BodyModelType::kCount:    break;
    }
    // Defensive fallback -- treat unknown types as membrane.
    return 500.0f * sizeDecay;
}

} // namespace Membrum::Bodies
