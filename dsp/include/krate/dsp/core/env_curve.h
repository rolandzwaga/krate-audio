// ==============================================================================
// Layer 0: Core Utility - Envelope Curve Shape Enum
// ==============================================================================
// The EnvCurve shape enum is shared by Layer 0 (curve_table.h's
// envCurveToCurveAmount) and Layer 1 (envelope_utils.h's target-ratio helpers).
// It lives here in Layer 0 so a Layer 0 consumer never has to reach up into a
// Layer 1 header for it (which would violate the strict layer-dependency rule,
// enforced by tools/lint-layers.js).
//
// Constitution Compliance:
// - Principle III: Modern C++ (scoped enum, fixed underlying type, C++20)
// - Principle IX: Layer 0 (depends only on the standard library)
// - Principle XIV: ODR Prevention (single definition, no duplication)
// ==============================================================================

#pragma once

#include <cstdint>

namespace Krate {
namespace DSP {

/// Curve shape for envelope stage transitions.
enum class EnvCurve : uint8_t {
    Exponential = 0,
    Linear,
    Logarithmic
};

} // namespace DSP
} // namespace Krate
