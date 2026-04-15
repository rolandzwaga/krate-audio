#include "membrane_inverse.h"

#include <algorithm>
#include <cmath>

namespace MembrumFit::MapperInversion {

namespace {

// MembraneMapper line 47: f0 = 500 * 0.1^size  =>  size = log10(500/f0).
float invertSize(float f0Hz) {
    if (f0Hz <= 0.0f) return 0.5f;
    const float s = std::log10(500.0f / f0Hz);
    return std::clamp(s, 0.0f, 1.0f);
}

// MembraneMapper line 88-92: decayTime = base * exp(lerp(log 0.3, log 3.0, decay)) * skewBias.
// Invert to decay norm given measured t60.
float invertDecay(float gammaFundamental) {
    if (gammaFundamental <= 0.0f) return 0.3f;
    const float t60 = std::log(1000.0f) / gammaFundamental;
    const float baseDecay = 0.5f;
    const float ratio = t60 / baseDecay;
    if (ratio <= 0.0f) return 0.3f;
    const float decayNorm = (std::log(ratio) - std::log(0.3f)) / (std::log(3.0f) - std::log(0.3f));
    return std::clamp(decayNorm, 0.0f, 1.0f);
}

}  // namespace

Membrum::PadConfig invertMembrane(const ModalDecomposition& modes,
                                  const AttackFeatures& features,
                                  double /*sampleRate*/) {
    Membrum::PadConfig cfg{};  // aggregate defaults: modeStretch=1/3, decaySkew=0.5, etc.
    cfg.bodyModel   = Membrum::BodyModelType::Membrane;
    cfg.exciterType = Membrum::ExciterType::Mallet;

    if (modes.modes.empty()) {
        cfg.size  = 0.5f;
        cfg.decay = 0.3f;
        return cfg;
    }

    const float f0    = modes.modes.front().freqHz;
    const float gamma = modes.modes.front().decayRate;
    cfg.size  = invertSize(f0);
    cfg.decay = invertDecay(gamma);

    // Material from brightness: ratio of high-mode to low-mode amplitude.
    float lowEng = 0.0f, highEng = 0.0f;
    for (const auto& m : modes.modes) {
        const float* dst = (m.freqHz < f0 * 3.0f) ? &lowEng : &highEng;
        const float val = m.amplitude * m.amplitude;
        const_cast<float*>(dst)[0] += val;
    }
    const float brightness = (lowEng + highEng > 0.0f) ? highEng / (lowEng + highEng) : 0.5f;
    cfg.material = std::clamp(brightness, 0.0f, 1.0f);

    cfg.strikePosition = std::clamp(0.3f + 0.4f * features.inharmonicity, 0.0f, 1.0f);
    cfg.level          = std::clamp(features.velocityEstimate, 0.05f, 1.0f);
    return cfg;
}

}  // namespace MembrumFit::MapperInversion
