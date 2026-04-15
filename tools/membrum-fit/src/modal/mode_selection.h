#pragma once

#include "../types.h"

#include <span>

namespace MembrumFit::Modal {

// MDL/ITC model-order selection over N in [minN, maxN]. Phase 1 returns a
// fixed midpoint; Phase 3 enables the real information-theoretic selector.
int selectModelOrder(std::span<const float> decay,
                     double sampleRate,
                     int    minN = 8,
                     int    maxN = 32);

}  // namespace MembrumFit::Modal
