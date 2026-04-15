#include "exciter_classifier.h"

namespace MembrumFit {

Membrum::ExciterType classifyExciter(const AttackFeatures& f, ExciterDecisionSet ds) {
    // Full 6-way tree per spec §4.4. Phase-1 set trims Friction/Feedback/FM.
    if (ds == ExciterDecisionSet::FullSixWay) {
        if (f.preOnsetARPeak > 0.35f && f.decayTailEnergyRatio > 0.5f)
            return Membrum::ExciterType::Feedback;
        if (f.logAttackTime < -2.7f && f.spectralFlatness < 0.2f && f.inharmonicity > 0.15f)
            return Membrum::ExciterType::FMImpulse;
        if (f.logAttackTime < -3.0f && f.spectralFlatness < 0.15f)
            return Membrum::ExciterType::Impulse;
        if (f.logAttackTime >  0.0f)
            return Membrum::ExciterType::Friction;
        if (f.spectralFlatness > 0.45f && f.peakSpectralCentroid > 2000.0f)
            return Membrum::ExciterType::NoiseBurst;
        return Membrum::ExciterType::Mallet;
    }

    // Phase-1 3-way: Impulse / Mallet / NoiseBurst only.
    if (f.logAttackTime < -3.0f && f.spectralFlatness < 0.15f)
        return Membrum::ExciterType::Impulse;
    if (f.spectralFlatness > 0.45f && f.peakSpectralCentroid > 2000.0f)
        return Membrum::ExciterType::NoiseBurst;
    return Membrum::ExciterType::Mallet;
}

}  // namespace MembrumFit
