#include "bobyqa_refine.h"

#if MEMBRUM_FIT_HAVE_NLOPT
#  include <nlopt.h>
#endif

#include <cstring>
#include <stdexcept>

namespace MembrumFit {

namespace {
// Layout: PadConfig fields treated as a flat float array. Indices 0/1 are
// discrete enums and not optimised here; indices 2..41 map to material(2),
// size(3), decay(4), strikePosition(5), level(6), tsFilterType(7), ...,
// macroComplexity(41). Matches pad_config.h offsets.
float* fieldPtr(Membrum::PadConfig& c, ParamIndex idx) {
    switch (idx) {
        case  2: return &c.material;
        case  3: return &c.size;
        case  4: return &c.decay;
        case  5: return &c.strikePosition;
        case  6: return &c.level;
        case  7: return &c.tsFilterType;
        case  8: return &c.tsFilterCutoff;
        case  9: return &c.tsFilterResonance;
        case 10: return &c.tsFilterEnvAmount;
        case 11: return &c.tsDriveAmount;
        case 12: return &c.tsFoldAmount;
        case 13: return &c.tsPitchEnvStart;
        case 14: return &c.tsPitchEnvEnd;
        case 15: return &c.tsPitchEnvTime;
        case 16: return &c.tsPitchEnvCurve;
        case 17: return &c.tsFilterEnvAttack;
        case 18: return &c.tsFilterEnvDecay;
        case 19: return &c.tsFilterEnvSustain;
        case 20: return &c.tsFilterEnvRelease;
        case 21: return &c.modeStretch;
        case 22: return &c.decaySkew;
        case 23: return &c.modeInjectAmount;
        case 24: return &c.nonlinearCoupling;
        case 25: return &c.morphEnabled;
        case 26: return &c.morphStart;
        case 27: return &c.morphEnd;
        case 28: return &c.morphDuration;
        case 29: return &c.morphCurve;
        case 32: return &c.fmRatio;
        case 33: return &c.feedbackAmount;
        case 34: return &c.noiseBurstDuration;
        case 35: return &c.frictionPressure;
        case 36: return &c.couplingAmount;
        case 37: return &c.macroTightness;
        case 38: return &c.macroBrightness;
        case 39: return &c.macroBodySize;
        case 40: return &c.macroPunch;
        case 41: return &c.macroComplexity;
        default: return nullptr;
    }
}
}  // namespace

std::vector<float> padConfigToVector(const Membrum::PadConfig& cfg,
                                     std::span<const ParamIndex> indices) {
    std::vector<float> out(indices.size());
    Membrum::PadConfig c = cfg;
    for (std::size_t i = 0; i < indices.size(); ++i) {
        const float* p = fieldPtr(c, indices[i]);
        out[i] = p ? *p : 0.0f;
    }
    return out;
}

void vectorToPadConfig(std::span<const float> x,
                       std::span<const ParamIndex> indices,
                       Membrum::PadConfig& cfg) {
    for (std::size_t i = 0; i < indices.size(); ++i) {
        if (float* p = fieldPtr(cfg, indices[i])) {
            *p = std::clamp(x[i], 0.0f, 1.0f);
        }
    }
}

#if MEMBRUM_FIT_HAVE_NLOPT
namespace {

struct EvalState {
    const RefineContext* ctx;
    RenderableMembrumVoice* voice;
    Membrum::PadConfig working;
    std::span<const float> targetClipped;
    std::vector<float> targetMfcc;
    std::vector<float> targetEnv;
    float renderSec;
    int evals;
    float bestLoss;
    Membrum::PadConfig bestCfg;
};

double bobyqaObjective(unsigned n, const double* x, double* /*grad*/, void* userData) {
    auto* st = static_cast<EvalState*>(userData);
    std::vector<float> xf(n);
    for (unsigned i = 0; i < n; ++i) xf[i] = static_cast<float>(x[i]);
    vectorToPadConfig(xf, st->ctx->optimisable, st->working);
    auto rendered = st->voice->renderToVector(st->working, /*vel*/ 1.0f, st->renderSec);
    if (rendered.size() > st->targetClipped.size()) rendered.resize(st->targetClipped.size());
    const float l = totalLoss(st->targetClipped, st->targetMfcc, st->targetEnv,
                              rendered, st->ctx->sampleRate, st->ctx->weights);
    if (l < st->bestLoss) { st->bestLoss = l; st->bestCfg = st->working; }
    ++st->evals;
    return static_cast<double>(l);
}
}  // namespace
#endif

RefineResult refineBOBYQA(const RefineContext& ctx, RenderableMembrumVoice& voice) {
    RefineResult r;
    r.final = ctx.initial;
    // Cap render length: max 0.5 s or the target's length, whichever is smaller.
    // BOBYQA evaluates hundreds of times; long renders dominate wall time.
    const float renderSec = std::min(0.5f,
        static_cast<float>(ctx.target.size() / std::max(ctx.sampleRate, 1.0)));
    auto initial = voice.renderToVector(ctx.initial, /*vel*/ 1.0f, renderSec);
    if (initial.size() > ctx.target.size()) initial.resize(ctx.target.size());
    const std::span<const float> tClipped(ctx.target.data(),
        std::min(ctx.target.size(), initial.size()));
    const auto tMfcc = computeMFCC(tClipped, ctx.sampleRate);
    const auto tEnv  = computeLogEnvelope(tClipped, ctx.sampleRate);
    r.initialLoss = totalLoss(tClipped, tMfcc, tEnv, initial, ctx.sampleRate, ctx.weights);
    r.finalLoss   = r.initialLoss;

#if MEMBRUM_FIT_HAVE_NLOPT
    if (ctx.optimisable.empty()) return r;

    EvalState st{};
    st.ctx = &ctx;
    st.voice = &voice;
    st.working = ctx.initial;
    st.targetClipped = tClipped;
    st.targetMfcc = tMfcc;
    st.targetEnv  = tEnv;
    st.renderSec = renderSec;
    st.evals = 0;
    st.bestLoss = r.initialLoss;
    st.bestCfg  = ctx.initial;

    const unsigned dim = static_cast<unsigned>(ctx.optimisable.size());
    nlopt_opt opt = nlopt_create(NLOPT_LN_BOBYQA, dim);
    if (!opt) return r;

    std::vector<double> lb(dim, 0.0), ub(dim, 1.0), x(dim, 0.0);
    auto x0 = padConfigToVector(ctx.initial, ctx.optimisable);
    for (unsigned i = 0; i < dim; ++i) x[i] = static_cast<double>(x0[i]);
    nlopt_set_lower_bounds(opt, lb.data());
    nlopt_set_upper_bounds(opt, ub.data());
    nlopt_set_min_objective(opt, bobyqaObjective, &st);
    nlopt_set_maxeval(opt, ctx.maxEvals);
    nlopt_set_xtol_rel(opt, 1e-4);

    double minF = 0.0;
    const auto rc = nlopt_optimize(opt, x.data(), &minF);
    nlopt_destroy(opt);

    r.final           = st.bestCfg;
    r.finalLoss       = st.bestLoss;
    r.evalCount       = st.evals;
    r.convergedBOBYQA = (rc > 0);
#endif
    return r;
}

}  // namespace MembrumFit
