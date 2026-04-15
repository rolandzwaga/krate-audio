#pragma once

#include "../types.h"

namespace MembrumFit::MapperInversion {

// Invert MembraneMapper to recover (material, size, decay, strikePosition)
// from measured modal decomposition. Emits normalised defaults for every
// field not touched (modeStretch=1/3, decaySkew=0.5, macros=0.5 etc.) per
// the FR-055-like invariant in specs/membrum-fit-tool.md §4.7.
Membrum::PadConfig invertMembrane(const ModalDecomposition& modes,
                                  const AttackFeatures& features,
                                  double sampleRate);

}  // namespace MembrumFit::MapperInversion
