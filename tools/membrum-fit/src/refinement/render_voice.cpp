#include "render_voice.h"

#include "dsp/drum_voice.h"
// The PadConfig -> DrumVoice mapping is the SHARED production helper --
// the fitter renders through the identical application surface the plugin
// uses (noise/click layers, damping overrides, airLoading, modeInject,
// wireCoupling, secondary shell, material morph included). A private,
// partial copy used to live here and silently dropped all of those
// (06-orchestralKit-fix-plan.md D1).
#include "dsp/pad_config_apply.h"

#include <cmath>

namespace MembrumFit {

struct RenderableMembrumVoice::Impl {
    Membrum::DrumVoice voice;
};

RenderableMembrumVoice::RenderableMembrumVoice() : impl_(std::make_unique<Impl>()) {}
RenderableMembrumVoice::~RenderableMembrumVoice() = default;

void RenderableMembrumVoice::prepare(double sampleRate, int blockSize) {
    sampleRate_ = sampleRate;
    blockSize_  = std::max(blockSize, 64);
    impl_->voice.prepare(sampleRate, /*voiceId*/ 0);
}

void RenderableMembrumVoice::render(const Membrum::PadConfig& cfg, float velocity, std::span<float> out) {
    auto& v = impl_->voice;
    Membrum::applyPadConfigToVoice(v, cfg);
    v.noteOn(std::clamp(velocity, 0.0f, 1.0f));
    int remaining = static_cast<int>(out.size());
    int offset = 0;
    while (remaining > 0) {
        const int n = std::min(blockSize_, remaining);
        v.processBlock(out.data() + offset, n);
        offset    += n;
        remaining -= n;
    }
    v.noteOff();
}

std::vector<float> RenderableMembrumVoice::renderToVector(const Membrum::PadConfig& cfg,
                                                          float velocity,
                                                          float lenSec) {
    const std::size_t n = static_cast<std::size_t>(std::round(lenSec * sampleRate_));
    std::vector<float> out(n, 0.0f);
    render(cfg, velocity, out);
    return out;
}

}  // namespace MembrumFit
