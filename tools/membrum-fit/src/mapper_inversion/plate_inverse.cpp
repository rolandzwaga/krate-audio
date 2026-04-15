#include "plate_inverse.h"

#include <algorithm>
#include <cmath>

namespace MembrumFit::MapperInversion {

Membrum::PadConfig invertPlate(const ModalDecomposition& modes,
                               const AttackFeatures& features,
                               double /*sampleRate*/) {
    Membrum::PadConfig cfg{};
    cfg.bodyModel   = Membrum::BodyModelType::Plate;
    cfg.exciterType = Membrum::ExciterType::Mallet;

    if (modes.modes.empty()) {
        cfg.size = 0.5f;  cfg.decay = 0.3f;  return cfg;
    }
    const float f0 = modes.modes.front().freqHz;
    const float g  = modes.modes.front().decayRate;
    // PlateMapper: f0 = 800 * 0.1^size.
    cfg.size = std::clamp(std::log10(800.0f / std::max(f0, 20.0f)), 0.0f, 1.0f);
    // Plate decay: longer sustain; same normalisation as membrane.
    if (g > 0.0f) {
        const float t60 = std::log(1000.0f) / g;
        cfg.decay = std::clamp(
            (std::log(t60 / 1.2f) - std::log(0.3f)) / (std::log(3.0f) - std::log(0.3f)),
            0.0f, 1.0f);
    } else {
        cfg.decay = 0.3f;
    }
    // Brightness: 0.5 + 0.5*material -> material = 2*brightness - 1.
    float lowEng = 0.0f, highEng = 0.0f;
    for (const auto& m : modes.modes) {
        const float val = m.amplitude * m.amplitude;
        if (m.freqHz < f0 * 3.0f) lowEng += val; else highEng += val;
    }
    const float brightness = (lowEng + highEng > 0.0f) ? highEng / (lowEng + highEng) : 0.5f;
    cfg.material = std::clamp(2.0f * brightness - 1.0f, 0.0f, 1.0f);
    cfg.strikePosition = std::clamp(0.3f + 0.4f * features.inharmonicity, 0.0f, 1.0f);
    cfg.level          = std::clamp(features.velocityEstimate, 0.05f, 1.0f);
    return cfg;
}

}  // namespace MembrumFit::MapperInversion
