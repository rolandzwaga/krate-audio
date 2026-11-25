#pragma once

#include <algorithm>
#include <cstdint>

// ==============================================================================
// BodyModelType -- Phase 2 body backend tag (data-model.md §1)
// ==============================================================================

namespace Membrum {

enum class BodyModelType : std::uint8_t
{
    Membrane  = 0,
    Plate     = 1,
    Shell     = 2,
    String    = 3,
    Bell      = 4,
    NoiseBody = 5,
    kCount    = 6,
};

static_assert(static_cast<int>(BodyModelType::kCount) == 6,
              "BodyModelType::kCount must reflect the number of alternatives");

/// Body-model normalized parameter value -> type, with the processor's clamp.
[[nodiscard]] inline BodyModelType decodeBodyModel(double norm) noexcept
{
    const int count = static_cast<int>(BodyModelType::kCount);
    return static_cast<BodyModelType>(
        std::clamp(static_cast<int>(norm * static_cast<double>(count)), 0, count - 1));
}

} // namespace Membrum
