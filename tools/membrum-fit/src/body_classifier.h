#pragma once

#include "types.h"

namespace MembrumFit {

// Body-model classifier. Phase 1 returns Membrane as the fast-path winner with
// full confidence. Phase 2 lights up the full 6-way mode-ratio scoring
// described in specs/membrum-fit-tool.md §4.6.
BodyScoreList classifyBody(const ModalDecomposition& modes,
                           const AttackFeatures& features);

Membrum::BodyModelType pickBestBody(const BodyScoreList& scores);

}  // namespace MembrumFit
