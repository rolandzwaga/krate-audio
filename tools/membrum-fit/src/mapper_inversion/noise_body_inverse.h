#pragma once

#include "../types.h"

namespace MembrumFit::MapperInversion {

Membrum::PadConfig invertNoiseBody(const ModalDecomposition& modes,
                                   const AttackFeatures& features,
                                   double sampleRate);

}  // namespace MembrumFit::MapperInversion
