// Smoke test for the --global CRS escape path. Verifies the CRS optimiser
// runs and produces a finite finalLoss; tuning quality is exercised by
// the [.corpus] sweep, not here.
#include "src/refinement/cmaes_refine.h"
#include "src/refinement/render_voice.h"

#include "dsp/default_kit.h"

#include <catch2/catch_test_macros.hpp>

#include <span>
#include <vector>

TEST_CASE("Global CRS: returns a finite RefineResult on Kick render") {
    constexpr double sr = 44100.0;

    Membrum::PadConfig ground{};
    Membrum::DefaultKit::applyTemplate(ground, Membrum::DrumTemplate::Kick);

    MembrumFit::RenderableMembrumVoice voice;
    voice.prepare(sr);
    const auto target = voice.renderToVector(ground, 1.0f, 0.3f);

    MembrumFit::RefineContext ctx;
    ctx.target = std::span<const float>(target.data(), target.size());
    ctx.sampleRate = sr;
    ctx.initial = ground;
    ctx.optimisable = { 2, 3, 4 };  // material, size, decay
    ctx.maxEvals = 30;              // tiny budget for CI

    const auto r = MembrumFit::refineGlobalCRS(ctx, voice);
    REQUIRE(std::isfinite(r.initialLoss));
    REQUIRE(std::isfinite(r.finalLoss));
    REQUIRE(r.finalLoss <= r.initialLoss + 1e-3f);
    REQUIRE(r.evalCount > 0);
}
