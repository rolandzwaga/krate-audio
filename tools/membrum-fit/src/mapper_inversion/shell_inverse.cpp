#include "shell_inverse.h"

namespace MembrumFit::MapperInversion {

Membrum::PadConfig invertShell(const ModalDecomposition& /*modes*/,
                               const AttackFeatures& /*features*/,
                               double /*sampleRate*/) {
    Membrum::PadConfig cfg{};
    cfg.bodyModel = Membrum::BodyModelType::Shell;
    return cfg;
}

}  // namespace MembrumFit::MapperInversion
