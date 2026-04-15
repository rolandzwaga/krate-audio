#pragma once

#include "types.h"

#include <span>

namespace MembrumFit {

// Fit Unnatural Zone parameters (modeInject, decaySkew, nonlinearCoupling,
// modeStretch, Material Morph) from the residual signal (target minus
// re-synthesised body). Phase 1 leaves defaults in place; Phase 3 enables
// the full residual analysis per specs/membrum-fit-tool.md §4.9.
void fitUnnaturalZone(std::span<const float> samples,
                      const SegmentedSample& seg,
                      double sampleRate,
                      const ModalDecomposition& modes,
                      const Membrum::PadConfig& fittedBody,
                      Membrum::PadConfig& config);

}  // namespace MembrumFit
