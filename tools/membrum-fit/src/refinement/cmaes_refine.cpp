#include "cmaes_refine.h"
#include "loss.h"

#if MEMBRUM_FIT_HAVE_NLOPT
#  include <nlopt.h>
#endif

#include <algorithm>
#include <span>
#include <vector>

namespace MembrumFit {

#if MEMBRUM_FIT_HAVE_NLOPT
namespace {

struct CrsState {
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

double crsObjective(unsigned n, const double* x, double* /*grad*/, void* userData) {
    auto* st = static_cast<CrsState*>(userData);
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

RefineResult refineGlobalCRS(const RefineContext& ctx, RenderableMembrumVoice& voice) {
    RefineResult r;
    r.final = ctx.initial;

    const float renderSec = std::min(0.5f,
        static_cast<float>(ctx.target.size() / std::max(ctx.sampleRate, 1.0)));
    auto initial = voice.renderToVector(ctx.initial, 1.0f, renderSec);
    if (initial.size() > ctx.target.size()) initial.resize(ctx.target.size());
    const std::span<const float> tClipped(ctx.target.data(),
        std::min(ctx.target.size(), initial.size()));
    const auto tMfcc = computeMFCC(tClipped, ctx.sampleRate);
    const auto tEnv  = computeLogEnvelope(tClipped, ctx.sampleRate);
    r.initialLoss = totalLoss(tClipped, tMfcc, tEnv, initial, ctx.sampleRate, ctx.weights);
    r.finalLoss   = r.initialLoss;

#if MEMBRUM_FIT_HAVE_NLOPT
    if (ctx.optimisable.empty()) return r;

    CrsState st{};
    st.ctx = &ctx;
    st.voice = &voice;
    st.working = ctx.initial;
    st.targetClipped = tClipped;
    st.targetMfcc = tMfcc;
    st.targetEnv = tEnv;
    st.renderSec = renderSec;
    st.evals = 0;
    st.bestLoss = r.initialLoss;
    st.bestCfg  = ctx.initial;

    const unsigned dim = static_cast<unsigned>(ctx.optimisable.size());
    nlopt_opt opt = nlopt_create(NLOPT_GN_CRS2_LM, dim);
    if (!opt) return r;

    std::vector<double> lb(dim, 0.0), ub(dim, 1.0), x(dim, 0.0);
    auto x0 = padConfigToVector(ctx.initial, ctx.optimisable);
    for (unsigned i = 0; i < dim; ++i) x[i] = static_cast<double>(x0[i]);
    nlopt_set_lower_bounds(opt, lb.data());
    nlopt_set_upper_bounds(opt, ub.data());
    nlopt_set_min_objective(opt, crsObjective, &st);
    nlopt_set_maxeval(opt, ctx.maxEvals);
    nlopt_set_xtol_rel(opt, 1e-3);

    double minF = 0.0;
    const auto rc = nlopt_optimize(opt, x.data(), &minF);
    nlopt_destroy(opt);

    r.final     = st.bestCfg;
    r.finalLoss = st.bestLoss;
    r.evalCount = st.evals;
    r.escapedCMAES = (rc > 0);
#endif
    return r;
}

}  // namespace MembrumFit
