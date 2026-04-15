#include "sfz_ingest.h"

namespace MembrumFit::Ingestion {

KitSpec loadKitSFZ(const std::filesystem::path& /*sfzPath*/) {
    // Phase 4 wires up sfizz parser. Return empty KitSpec for Phase 1-3.
    return {};
}

}  // namespace MembrumFit::Ingestion
