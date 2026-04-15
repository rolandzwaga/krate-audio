#pragma once

#include "../types.h"

namespace MembrumFit::MapperInversion {

// Note: StringMapper has semantic-inverted brightness (brightness = 1 - material).
// The inversion MUST preserve that convention.
Membrum::PadConfig invertString(const ModalDecomposition& modes,
                                const AttackFeatures& features,
                                double sampleRate);

}  // namespace MembrumFit::MapperInversion
