// BOBYQA refinement smoke: starting from a perturbed PadConfig, optimising
// {material, size, decay} should NOT increase the loss (monotonic
// improvement under bounded eval budget).
#include "src/refinement/bobyqa_refine.h"
#include "src/refinement/render_voice.h"

#include "dsp/default_kit.h"

#include <catch2/catch_approx.hpp>
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

// Phase-8 field coverage: padConfigToVector / vectorToPadConfig must round-trip
// every offset registered in fieldPtr(), including the Phase 8 fields
// (50..58). Catches any missing case in the switch that would silently map to
// nullptr and drop the parameter during optimisation.
TEST_CASE("BOBYQA: Phase 8 fields round-trip through fieldPtr switch") {
    using MembrumFit::padConfigToVector;
    using MembrumFit::vectorToPadConfig;

    Membrum::PadConfig cfg{};
    cfg.bodyDampingB1    = 0.11f;
    cfg.bodyDampingB3    = 0.22f;
    cfg.airLoading       = 0.33f;
    cfg.modeScatter      = 0.44f;
    cfg.couplingStrength = 0.55f;
    cfg.secondaryEnabled = 1.0f;
    cfg.secondarySize    = 0.66f;
    cfg.secondaryMaterial= 0.77f;
    cfg.tensionModAmt    = 0.88f;

    const std::vector<MembrumFit::ParamIndex> all8 = {50, 51, 52, 53, 54, 56, 57, 58};
    const auto v = padConfigToVector(cfg, all8);
    REQUIRE(v.size() == 8);
    Membrum::PadConfig out{};
    vectorToPadConfig(v, all8, out);
    REQUIRE(out.bodyDampingB1    == Catch::Approx(0.11f));
    REQUIRE(out.bodyDampingB3    == Catch::Approx(0.22f));
    REQUIRE(out.airLoading       == Catch::Approx(0.33f));
    REQUIRE(out.modeScatter      == Catch::Approx(0.44f));
    REQUIRE(out.couplingStrength == Catch::Approx(0.55f));
    REQUIRE(out.secondarySize    == Catch::Approx(0.66f));
    REQUIRE(out.secondaryMaterial== Catch::Approx(0.77f));
    REQUIRE(out.tensionModAmt    == Catch::Approx(0.88f));
}

// Higher-dimensional BOBYQA: starting from a perturbed PadConfig, optimising
// the 14-parameter Phase-8 set should still monotonically improve loss.
// Guards against regressions in the extended optimisable map and in the
// sentinel-seed logic that main.cpp applies before handing the context to
// refineBOBYQA (here we mirror that seeding explicitly).
TEST_CASE("BOBYQA: 14D Phase-8 refinement never increases loss") {
    constexpr double sr = 44100.0;
    Membrum::PadConfig ground{};
    Membrum::DefaultKit::applyTemplate(ground, Membrum::DrumTemplate::Tom);

    MembrumFit::RenderableMembrumVoice voice;
    voice.prepare(sr);
    const auto target = voice.renderToVector(ground, 1.0f, 0.3f);

    Membrum::PadConfig start = ground;
    start.material         = std::min(1.0f, ground.material + 0.15f);
    start.size             = std::max(0.0f, ground.size     - 0.10f);
    start.couplingStrength = std::max(0.0f, ground.couplingStrength - 0.10f);
    start.airLoading       = std::min(1.0f, ground.airLoading + 0.10f);
    // Same sentinel-seed logic main.cpp applies.
    if (start.bodyDampingB1 < 0.0f) start.bodyDampingB1 = 0.5f;
    if (start.bodyDampingB3 < 0.0f) start.bodyDampingB3 = 0.5f;

    MembrumFit::RefineContext ctx;
    ctx.target = std::span<const float>(target.data(), target.size());
    ctx.sampleRate = sr;
    ctx.initial = start;
    // 6 original + 8 Phase 8 continuous = 14D.
    ctx.optimisable = { 2, 3, 4, 8, 9, 15,
                        50, 51, 52, 53, 54, 56, 57, 58 };
    ctx.maxEvals = 150;  // (n+2)(n+1)/2 = 120 initial + a little room.

    const auto r = MembrumFit::refineBOBYQA(ctx, voice);
    REQUIRE(std::isfinite(r.initialLoss));
    REQUIRE(std::isfinite(r.finalLoss));
    REQUIRE(r.finalLoss <= r.initialLoss + 1e-3f);
    REQUIRE(r.evalCount > 0);
}
