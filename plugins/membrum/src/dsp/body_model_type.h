#pragma once

// ==============================================================================
// BodyModelType -- Phase 2 body backend tag (data-model.md §1)
// ==============================================================================

namespace Membrum {

enum class BodyModelType : int
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

} // namespace Membrum
