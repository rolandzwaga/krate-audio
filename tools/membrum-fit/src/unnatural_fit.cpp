#include "unnatural_fit.h"

#include <algorithm>
#include <cmath>

namespace MembrumFit {

namespace {

// Categorise a mode as "natural" (close to an integer harmonic of the
// fundamental) vs "injected" (off the harmonic series by >= 10 %).
bool isNaturalHarmonic(float freqHz, float fundamentalHz) {
    if (fundamentalHz <= 0.0f) return true;
    const float ratio = freqHz / fundamentalHz;
    const float nearest = std::round(ratio);
    if (nearest < 1.0f) return true;  // skip sub-harmonics
    return std::abs(ratio - nearest) / nearest < 0.1f;
}

}  // namespace

void fitUnnaturalZone(std::span<const float> /*samples*/,
                      const SegmentedSample& /*seg*/,
                      double /*sr*/,
                      const ModalDecomposition& modes,
                      const Membrum::PadConfig& /*fittedBody*/,
                      Membrum::PadConfig& cfg) {
    // Defaults already live in cfg via PadConfig's aggregate init:
    //   modeStretch=0.333333 (norm of [0.5, 2.0], default 1.0)
    //   decaySkew  =0.5 (norm of [-1, 1], default 0.0)
    //   modeInjectAmount=0.0, nonlinearCoupling=0.0

    if (modes.modes.size() < 3) return;

    const float f0 = modes.modes.front().freqHz;
    float injectedEnergy = 0.0f, naturalEnergy = 0.0f;
    for (const auto& m : modes.modes) {
        const float e = m.amplitude * m.amplitude;
        if (isNaturalHarmonic(m.freqHz, f0)) naturalEnergy  += e;
        else                                  injectedEnergy += e;
    }
    const float total = naturalEnergy + injectedEnergy;
    const float ratio = (total > 1e-9f) ? injectedEnergy / total : 0.0f;
    // Map [0, 0.4] ratio -> [0, 1] normalised mode inject amount (spec §4.9).
    cfg.modeInjectAmount = std::clamp(ratio / 0.4f, 0.0f, 1.0f);

    // Decay skew: if high modes decay SLOWER than low modes (inverted), set
    // a negative skew. We measure the median decayRate among low (< 2*f0) vs
    // high (>= 2*f0) modes.
    std::vector<float> lowGs, highGs;
    for (const auto& m : modes.modes) {
        if (m.freqHz < 2.0f * f0) lowGs.push_back(m.decayRate);
        else                      highGs.push_back(m.decayRate);
    }
    if (!lowGs.empty() && !highGs.empty()) {
        std::sort(lowGs.begin(), lowGs.end());
        std::sort(highGs.begin(), highGs.end());
        const float medLow  = lowGs[lowGs.size() / 2];
        const float medHigh = highGs[highGs.size() / 2];
        if (medLow > 1e-6f) {
            const float logRatio = std::log(medHigh / medLow);
            // Normalised decaySkew in [0, 1] where 0.5 = unity. Clamp to avoid extreme values.
            const float skewNorm = std::clamp(0.5f + 0.25f * logRatio, 0.0f, 1.0f);
            cfg.decaySkew = skewNorm;
        }
    }
    // nonlinearCoupling is hard to estimate from a single hit; leave at 0.
}

}  // namespace MembrumFit
