#include "render_voice.h"

#include "dsp/drum_voice.h"
#include "dsp/tone_shaper.h"

#include <cmath>

namespace MembrumFit {

namespace {

// Denormalisation helpers matching plugins/membrum/src/processor/processor.cpp.
// PadConfig stores normalised [0,1] floats; the production processor maps
// them to physical units before pushing to DrumVoice. The fitter must use the
// SAME mapping or its rendered loss surface diverges from runtime behaviour.
inline float denormCutoffHz(float n)   { return 20.0f * std::pow(1000.0f, std::clamp(n, 0.0f, 1.0f)); }
inline float denormResonance(float n)  { return 0.7f + 9.3f * std::clamp(n, 0.0f, 1.0f); }
inline float denormPitchHz(float n)    { return 20.0f * std::pow(100.0f, std::clamp(n, 0.0f, 1.0f)); }
inline float denormPitchTimeMs(float n){ return 500.0f * std::clamp(n, 0.0f, 1.0f); }
inline float denormFilterEnvMs(float n){ return 5000.0f * std::clamp(n, 0.0f, 1.0f); }
inline float denormFilterEnvAmt(float n){ return 2.0f * std::clamp(n, 0.0f, 1.0f) - 1.0f; }
inline float denormModeStretch(float n){ return 0.5f + 1.5f * std::clamp(n, 0.0f, 1.0f); }
inline float denormDecaySkew(float n)  { return 2.0f * std::clamp(n, 0.0f, 1.0f) - 1.0f; }

void applyPadConfig(Membrum::DrumVoice& v, const Membrum::PadConfig& cfg) {
    v.setExciterType(cfg.exciterType);
    v.setBodyModel(cfg.bodyModel);
    v.setMaterial(cfg.material);
    v.setSize(cfg.size);
    v.setDecay(cfg.decay);
    v.setStrikePosition(cfg.strikePosition);
    v.setLevel(cfg.level);

    auto& ts = v.toneShaper();
    const int filterTypeI = static_cast<int>(std::round(cfg.tsFilterType * 2.0f));
    ts.setFilterType(static_cast<Membrum::ToneShaperFilterType>(std::clamp(filterTypeI, 0, 2)));
    ts.setFilterCutoff(denormCutoffHz(cfg.tsFilterCutoff));
    ts.setFilterResonance(denormResonance(cfg.tsFilterResonance));
    ts.setFilterEnvAmount(denormFilterEnvAmt(cfg.tsFilterEnvAmount));
    ts.setDriveAmount(std::clamp(cfg.tsDriveAmount, 0.0f, 1.0f));
    ts.setFoldAmount(std::clamp(cfg.tsFoldAmount, 0.0f, 1.0f));
    ts.setPitchEnvStartHz(denormPitchHz(cfg.tsPitchEnvStart));
    ts.setPitchEnvEndHz(denormPitchHz(cfg.tsPitchEnvEnd));
    ts.setPitchEnvTimeMs(denormPitchTimeMs(cfg.tsPitchEnvTime));
    ts.setPitchEnvCurve(cfg.tsPitchEnvCurve > 0.5f
                        ? Membrum::ToneShaperCurve::Linear
                        : Membrum::ToneShaperCurve::Exponential);
    ts.setFilterEnvAttackMs(denormFilterEnvMs(cfg.tsFilterEnvAttack));
    ts.setFilterEnvDecayMs (denormFilterEnvMs(cfg.tsFilterEnvDecay));
    ts.setFilterEnvSustain (std::clamp(cfg.tsFilterEnvSustain, 0.0f, 1.0f));
    ts.setFilterEnvReleaseMs(denormFilterEnvMs(cfg.tsFilterEnvRelease));

    auto& uz = v.unnaturalZone();
    uz.setModeStretch(denormModeStretch(cfg.modeStretch));
    uz.setDecaySkew  (denormDecaySkew  (cfg.decaySkew));
    // mode inject / nonlinear coupling have their own setter API; Phase 3 hooks them up.
}

}  // namespace

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
    applyPadConfig(v, cfg);
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
