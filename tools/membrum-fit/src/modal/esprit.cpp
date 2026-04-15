#include "esprit.h"

namespace MembrumFit::Modal {

ModalDecomposition extractModesESPRIT(std::span<const float> /*decay*/,
                                      double /*sampleRate*/,
                                      int    /*maxModes*/) {
    // Phase 3 enables TLS ESPRIT per spec §4.5. Phase 1 returns empty.
    return {};
}

}  // namespace MembrumFit::Modal
