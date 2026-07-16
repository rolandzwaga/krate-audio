#pragma once

// ==============================================================================
// ExciterType -- Phase 2 exciter backend tag (data-model.md §1)
// ==============================================================================

namespace Membrum {

enum class ExciterType : int
{
    Impulse    = 0,
    Mallet     = 1,
    NoiseBurst = 2,
    Friction   = 3,
    FMImpulse  = 4,
    Feedback   = 5,
    Clap       = 6,
    kCount     = 7,
};

static_assert(static_cast<int>(ExciterType::kCount) == 7,
              "ExciterType::kCount must reflect the number of alternatives");

} // namespace Membrum
