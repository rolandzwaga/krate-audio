#include "mode_selection.h"

#include <algorithm>

namespace MembrumFit::Modal {

int selectModelOrder(std::span<const float> /*decay*/,
                     double /*sampleRate*/,
                     int    minN,
                     int    maxN) {
    // Phase 1: fixed midpoint order. Phase 3 implements MDL/ITC per spec §2a.
    return std::clamp((minN + maxN) / 2, minN, maxN);
}

}  // namespace MembrumFit::Modal
