#pragma once

#include "../types.h"

#include <span>

namespace MembrumFit::Modal {

// Total-Least-Squares ESPRIT. Phase-1 stub returns an empty decomposition;
// Phase 3 (spec §8) lights up the real implementation.
ModalDecomposition extractModesESPRIT(std::span<const float> decay,
                                      double sampleRate,
                                      int    maxModes);

}  // namespace MembrumFit::Modal
