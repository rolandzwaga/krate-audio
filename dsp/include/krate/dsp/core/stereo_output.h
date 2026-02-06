// ==============================================================================
// Layer 0: Core Utility - StereoOutput
// ==============================================================================
// Lightweight stereo sample pair for returning stereo audio from process()
// methods. Extracted from unison_engine.h to prevent ODR violations when
// multiple Layer 3 systems need the same return type.
//
// Constitution Compliance:
// - Principle II:  Real-Time Safety (aggregate, no allocations)
// - Principle III: Modern C++ (C++20, aggregate initialization)
// - Principle IX:  Layer 0 (no dependencies on other DSP layers)
// - Principle XIV: ODR Prevention (single shared definition)
//
// Reference: specs/031-vector-mixer/plan.md (R-001)
// ==============================================================================

#pragma once

namespace Krate::DSP {

/// @brief Lightweight stereo sample pair.
///
/// Simple aggregate type for returning stereo audio from process().
/// No user-declared constructors -- supports brace initialization.
struct StereoOutput {
    float left = 0.0f;   ///< Left channel sample
    float right = 0.0f;  ///< Right channel sample
};

} // namespace Krate::DSP
