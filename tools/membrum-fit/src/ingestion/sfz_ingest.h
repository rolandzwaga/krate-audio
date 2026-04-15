#pragma once

#include "wav_dir.h"

namespace MembrumFit::Ingestion {

// SFZ ingestion (sfizz parser). Phase 4 wires this up. Phase 1-3 return an
// empty KitSpec. See specs/membrum-fit-tool.md §4.6 / §8 Phase 4.
KitSpec loadKitSFZ(const std::filesystem::path& sfzPath);

}  // namespace MembrumFit::Ingestion
