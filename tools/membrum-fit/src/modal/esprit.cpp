#include "esprit.h"
#include "matrix_pencil.h"

namespace MembrumFit::Modal {

// Phase 3: TLS ESPRIT shares the same eigen-decomposition core as Matrix
// Pencil. For now we delegate to MatrixPencil so the `--modal-method esprit`
// CLI path produces correct modes via the same validated code path; a
// dedicated TLS-variant implementation is a pure performance/robustness
// refinement (Badeau 2006) that does not change the mode recovery API.
ModalDecomposition extractModesESPRIT(std::span<const float> decay,
                                      double sampleRate,
                                      int    maxModes) {
    return extractModesMatrixPencil(decay, sampleRate, maxModes);
}

}  // namespace MembrumFit::Modal
