#pragma once

#include "types.h"

namespace MembrumFit {

// Hand-crafted rule tree per specs/membrum-fit-tool.md §4.4.
// Phase 1 restricts the decision set to {Impulse, Mallet, NoiseBurst}; later
// phases enable Friction, Feedback, and FMImpulse.
enum class ExciterDecisionSet {
    Phase1Subset,  // Impulse / Mallet / NoiseBurst only
    FullSixWay,    // Phase 2+: adds Friction, Feedback, FMImpulse
};

Membrum::ExciterType classifyExciter(const AttackFeatures& f,
                                     ExciterDecisionSet ds = ExciterDecisionSet::FullSixWay);

}  // namespace MembrumFit
