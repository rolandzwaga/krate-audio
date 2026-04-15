#include "plate_inverse.h"

namespace MembrumFit::MapperInversion {

Membrum::PadConfig invertPlate(const ModalDecomposition& /*modes*/,
                               const AttackFeatures& /*features*/,
                               double /*sampleRate*/) {
    // Phase 2 implements full plate inversion (f0 = 800 * 0.1^size).
    Membrum::PadConfig cfg{};
    cfg.bodyModel = Membrum::BodyModelType::Plate;
    return cfg;
}

}  // namespace MembrumFit::MapperInversion
