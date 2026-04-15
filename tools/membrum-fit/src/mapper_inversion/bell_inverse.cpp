#include "bell_inverse.h"

#include "dsp/bodies/bell_modes.h"

#include <algorithm>
#include <cmath>

namespace MembrumFit::MapperInversion {

Membrum::PadConfig invertBell(const ModalDecomposition& modes,
                              const AttackFeatures& features,
                              double /*sampleRate*/) {
    Membrum::PadConfig cfg{};
    cfg.bodyModel   = Membrum::BodyModelType::Bell;
    cfg.exciterType = Membrum::ExciterType::Mallet;

    if (modes.modes.empty()) { cfg.size = 0.5f; cfg.decay = 0.3f; return cfg; }

    // Bell's nominal is ratio=1.000 (index 4 in kBellRatios). The lowest
    // measured partial is typically the hum (ratio=0.25). Recover the
    // nominal by scaling the measured fundamental by 4 (hum -> nominal).
    // If the classifier was confident it picked Bell, the measured modes
    // will cluster near the kBellRatios series.
    // Take the amplitude-peak mode as the nominal candidate, fallback to the
    // lowest frequency.
    float nominalHz = modes.modes.front().freqHz;  // sorted by amplitude
    // BellMapper: f_nominal = 800 * 0.1^size.
    cfg.size = std::clamp(std::log10(800.0f / std::max(nominalHz, 20.0f)), 0.0f, 1.0f);
    const float g = modes.modes.front().decayRate;
    if (g > 0.0f) {
        const float t60 = std::log(1000.0f) / g;
        cfg.decay = std::clamp(
            (std::log(t60 / 2.0f) - std::log(0.3f)) / (std::log(3.0f) - std::log(0.3f)),
            0.0f, 1.0f);
    } else {
        cfg.decay = 0.3f;
    }
    // Bell brightness: 0.7 + 0.3 * material -> material = (b - 0.7) / 0.3.
    float lowEng = 0.0f, highEng = 0.0f;
    for (const auto& m : modes.modes) {
        const float val = m.amplitude * m.amplitude;
        if (m.freqHz < nominalHz * 3.0f) lowEng += val; else highEng += val;
    }
    const float brightness = (lowEng + highEng > 0.0f) ? highEng / (lowEng + highEng) : 0.7f;
    cfg.material = std::clamp((brightness - 0.7f) / 0.3f, 0.0f, 1.0f);
    cfg.strikePosition = std::clamp(0.3f + 0.4f * features.inharmonicity, 0.0f, 1.0f);
    cfg.level          = std::clamp(features.velocityEstimate, 0.05f, 1.0f);
    return cfg;
}

}  // namespace MembrumFit::MapperInversion
