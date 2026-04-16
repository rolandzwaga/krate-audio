// BOBYQA refinement smoke: starting from a perturbed PadConfig, optimising
// {material, size, decay} should NOT increase the loss (monotonic
// improvement under bounded eval budget).
#include "src/refinement/bobyqa_refine.h"
#include "src/refinement/render_voice.h"

#include "dsp/default_kit.h"

#include <catch2/catch_test_macros.hpp>

#include <span>

TEST_CASE("BOBYQA: never increases loss vs initial point") {
    constexpr double sr = 44100.0;
    Membrum::PadConfig ground{};
    Membrum::DefaultKit::applyTemplate(ground, Membrum::DrumTemplate::Kick);

    MembrumFit::RenderableMembrumVoice voice;
    voice.prepare(sr);
    const auto target = voice.renderToVector(ground, 1.0f, 0.3f);

    // Perturb starting point so BOBYQA has room to improve.
    Membrum::PadConfig start = ground;
    start.material = std::min(1.0f, ground.material + 0.2f);
    start.size     = std::max(0.0f, ground.size     - 0.15f);

    MembrumFit::RefineContext ctx;
    ctx.target = std::span<const float>(target.data(), target.size());
    ctx.sampleRate = sr;
    ctx.initial = start;
    ctx.optimisable = { 2, 3, 4 };  // material, size, decay
    ctx.maxEvals = 30;

    const auto r = MembrumFit::refineBOBYQA(ctx, voice);
    REQUIRE(std::isfinite(r.initialLoss));
    REQUIRE(std::isfinite(r.finalLoss));
    REQUIRE(r.finalLoss <= r.initialLoss + 1e-3f);
    REQUIRE(r.evalCount > 0);
}
