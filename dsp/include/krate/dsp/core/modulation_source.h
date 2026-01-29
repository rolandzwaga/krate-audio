// ==============================================================================
// Layer 0: Core Interface - Modulation Source
// ==============================================================================
// Abstract interface for modulation sources.
//
// Extracted from Layer 3 modulation_matrix.h to Layer 0 so that Layer 2
// processors (ChaosModSource, TransientDetector, etc.) can implement it
// without depending on Layer 3.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept virtual interface)
// - Principle III: Modern C++ (C++20, [[nodiscard]])
// - Principle IX: Layer 0 (no dependencies on higher layers)
//
// Reference: specs/008-modulation-system/spec.md
// ==============================================================================

#pragma once

#include <utility>

namespace Krate {
namespace DSP {

/// @brief Abstract interface for modulation sources.
///
/// Any class that can provide modulation values should implement this interface.
/// Known implementations: LFO (Layer 1), EnvelopeFollower (Layer 2),
/// ChaosModSource (Layer 2), RandomSource (Layer 2), SampleHoldSource (Layer 2),
/// PitchFollowerSource (Layer 2), TransientDetector (Layer 2).
class ModulationSource {
public:
    virtual ~ModulationSource() = default;

    /// @brief Get the current modulation output value.
    /// @return Current value (typically [-1,+1] for bipolar, [0,1] for unipolar)
    [[nodiscard]] virtual float getCurrentValue() const noexcept = 0;

    /// @brief Get the output range of this source.
    /// @return Pair of (minValue, maxValue)
    [[nodiscard]] virtual std::pair<float, float> getSourceRange() const noexcept = 0;
};

}  // namespace DSP
}  // namespace Krate
