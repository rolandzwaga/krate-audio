#include "string_inverse.h"

namespace MembrumFit::MapperInversion {

Membrum::PadConfig invertString(const ModalDecomposition& /*modes*/,
                                const AttackFeatures& /*features*/,
                                double /*sampleRate*/) {
    // Phase 2: implement with semantic-inversion of brightness=1-material
    // (string_mapper.h:53).
    Membrum::PadConfig cfg{};
    cfg.bodyModel = Membrum::BodyModelType::String;
    return cfg;
}

}  // namespace MembrumFit::MapperInversion
