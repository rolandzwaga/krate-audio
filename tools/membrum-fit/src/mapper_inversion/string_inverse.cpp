#include "string_inverse.h"

#include <algorithm>
#include <cmath>

namespace MembrumFit::MapperInversion {

// StringMapper: fundamental is the measured f0 directly (no logarithmic
// size mapping). Brightness is semantic-inverted inside the mapper:
// brightness = 1 - material (string_mapper.h:53). The inversion MUST
// preserve this convention.
Membrum::PadConfig invertString(const ModalDecomposition& modes,
                                const AttackFeatures& features,
                                double /*sampleRate*/) {
    Membrum::PadConfig cfg{};
    cfg.bodyModel   = Membrum::BodyModelType::String;
    cfg.exciterType = Membrum::ExciterType::Mallet;

    if (modes.modes.empty()) { cfg.size = 0.5f; cfg.decay = 0.3f; return cfg; }
    const float f0 = modes.modes.front().freqHz;
    const float g  = modes.modes.front().decayRate;
    // StringMapper inside f_map(size): f0 = 800 * 0.1^size clamped [20,8000].
    cfg.size = std::clamp(std::log10(800.0f / std::max(f0, 20.0f)), 0.0f, 1.0f);
    if (g > 0.0f) {
        const float t60 = std::log(1000.0f) / g;
        cfg.decay = std::clamp(
            (std::log(t60 / 1.5f) - std::log(0.3f)) / (std::log(3.0f) - std::log(0.3f)),
            0.0f, 1.0f);
    } else {
        cfg.decay = 0.3f;
    }
    // measure brightness, then invert: material = 1 - brightness.
    float lowEng = 0.0f, highEng = 0.0f;
    for (const auto& m : modes.modes) {
        const float val = m.amplitude * m.amplitude;
        if (m.freqHz < f0 * 3.0f) lowEng += val; else highEng += val;
    }
    const float brightness = (lowEng + highEng > 0.0f) ? highEng / (lowEng + highEng) : 0.5f;
    cfg.material = std::clamp(1.0f - brightness, 0.0f, 1.0f);
    cfg.strikePosition = std::clamp(0.3f + 0.4f * features.inharmonicity, 0.0f, 1.0f);
    cfg.level          = std::clamp(features.velocityEstimate, 0.05f, 1.0f);
    return cfg;
}

}  // namespace MembrumFit::MapperInversion
