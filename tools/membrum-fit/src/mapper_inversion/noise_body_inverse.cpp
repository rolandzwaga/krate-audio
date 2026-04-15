#include "noise_body_inverse.h"

#include <algorithm>
#include <cmath>

namespace MembrumFit::MapperInversion {

// NoiseBodyMapper is hybrid: a plate-like modal block plus a noise filter.
// We reuse plate-style size inversion; the material value controls the
// noise-filter cutoff (1500 + 5000*material Hz per noise_body_mapper.h:121).
Membrum::PadConfig invertNoiseBody(const ModalDecomposition& modes,
                                   const AttackFeatures& features,
                                   double /*sampleRate*/) {
    Membrum::PadConfig cfg{};
    cfg.bodyModel   = Membrum::BodyModelType::NoiseBody;
    cfg.exciterType = Membrum::ExciterType::NoiseBurst;

    if (modes.modes.empty()) { cfg.size = 0.5f; cfg.decay = 0.5f; return cfg; }
    const float f0 = modes.modes.front().freqHz;
    cfg.size = std::clamp(std::log10(1500.0f / std::max(f0, 20.0f)), 0.0f, 1.0f);

    // Material: invert the noise-filter cutoff mapping using the attack
    // spectral centroid as a proxy for the filter centre.
    const float cutoffHz = std::clamp(features.peakSpectralCentroid, 1500.0f, 6500.0f);
    cfg.material = std::clamp((cutoffHz - 1500.0f) / 5000.0f, 0.0f, 1.0f);

    const float g = modes.modes.front().decayRate;
    if (g > 0.0f) {
        const float t60 = std::log(1000.0f) / g;
        cfg.decay = std::clamp(
            (std::log(t60 / 0.5f) - std::log(0.3f)) / (std::log(3.0f) - std::log(0.3f)),
            0.0f, 1.0f);
    } else {
        cfg.decay = 0.5f;
    }
    cfg.strikePosition = 0.3f;
    cfg.level          = std::clamp(features.velocityEstimate, 0.05f, 1.0f);
    return cfg;
}

}  // namespace MembrumFit::MapperInversion
