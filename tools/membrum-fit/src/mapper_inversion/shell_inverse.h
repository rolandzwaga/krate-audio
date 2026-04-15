#pragma once

#include "../types.h"

namespace MembrumFit::MapperInversion {

Membrum::PadConfig invertShell(const ModalDecomposition& modes,
                               const AttackFeatures& features,
                               double sampleRate);

}  // namespace MembrumFit::MapperInversion
