#include "noise_body_inverse.h"

namespace MembrumFit::MapperInversion {

Membrum::PadConfig invertNoiseBody(const ModalDecomposition& /*modes*/,
                                   const AttackFeatures& /*features*/,
                                   double /*sampleRate*/) {
    Membrum::PadConfig cfg{};
    cfg.bodyModel = Membrum::BodyModelType::NoiseBody;
    return cfg;
}

}  // namespace MembrumFit::MapperInversion
