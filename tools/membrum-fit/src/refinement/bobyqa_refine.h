#pragma once

#include "../types.h"
#include "loss.h"
#include "render_voice.h"

#include <span>
#include <vector>

namespace MembrumFit {

// Indexes into PadConfig treated as a flat float32 array of optimisable
// parameters. The indices map to (exciterType(0), bodyModel(1), then 40
// continuous params at offsets 2..41 in pad_config.h).
using ParamIndex = int;

struct RefineContext {
    std::span<const float>    target;
    double                    sampleRate = 44100.0;
    Membrum::PadConfig        initial{};
    std::vector<ParamIndex>   optimisable;        // subset of [2..41]
    LossWeights               weights{};
    int                       maxEvals = 300;
    float                     earlyStopRelLoss = 0.01f;  // 1 %
    int                       earlyStopWindow  = 20;
};

struct RefineResult {
    Membrum::PadConfig final{};
    float              initialLoss = 0.0f;
    float              finalLoss   = 0.0f;
    int                evalCount   = 0;
    bool               convergedBOBYQA = false;
    bool               escapedCMAES    = false;
};

// Run derivative-free BOBYQA over the provided optimisable parameter indices.
// Frozen parameters (discrete selectors, macros, morph block) are kept at
// their initial values.
RefineResult refineBOBYQA(const RefineContext& ctx,
                          RenderableMembrumVoice& voice);

// Extract / apply a flat float vector to the optimisable parameters of a
// PadConfig. Exposed for tests.
std::vector<float> padConfigToVector(const Membrum::PadConfig& cfg,
                                     std::span<const ParamIndex> indices);
void vectorToPadConfig(std::span<const float> x,
                       std::span<const ParamIndex> indices,
                       Membrum::PadConfig& cfg);

}  // namespace MembrumFit
