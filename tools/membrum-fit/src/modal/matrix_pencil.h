#pragma once

#include "../types.h"

#include <span>

namespace MembrumFit::Modal {

// Extract up to maxModes damped complex exponentials from `decay` using Hua &
// Sarkar 1990 Matrix Pencil. Rejects modes with γ<0 (numerical artefacts).
// See specs/membrum-fit-tool.md §4.5 for the full contract.
ModalDecomposition extractModesMatrixPencil(std::span<const float> decay,
                                            double sampleRate,
                                            int    maxModes);

}  // namespace MembrumFit::Modal
