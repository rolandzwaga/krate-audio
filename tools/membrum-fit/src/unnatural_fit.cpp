#include "unnatural_fit.h"

namespace MembrumFit {

void fitUnnaturalZone(std::span<const float> /*samples*/,
                      const SegmentedSample& /*seg*/,
                      double /*sr*/,
                      const ModalDecomposition& /*modes*/,
                      const Membrum::PadConfig& /*fittedBody*/,
                      Membrum::PadConfig& /*config*/) {
    // Phase 1: leave PadConfig defaults (modeStretch=0.333333, decaySkew=0.5,
    // modeInjectAmount=0.0, nonlinearCoupling=0.0) in place. Phase 3 lights up
    // the full residual analysis per spec §4.9.
}

}  // namespace MembrumFit
