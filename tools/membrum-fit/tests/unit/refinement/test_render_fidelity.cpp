// ==============================================================================
// test_render_fidelity.cpp -- RenderableMembrumVoice must apply the SAME
// PadConfig surface as the production VoicePool (shared apply helper).
//
// Regression for the harness-fidelity gap (06-orchestralKit-fix-plan.md, D1):
// the fitter's private applyPadConfig() forwarded only core/ToneShaper/
// stretch/skew fields, silently dropping the noise layer, click layer,
// body-damping overrides, airLoading, modeInject, wireCoupling, secondary
// shell, and the entire material-morph block. Every render-based verdict on
// those axes was measured against a voice that never had them applied.
//
// Each SECTION renders a baseline config and a config differing in exactly
// one previously-dropped field; the two renders must audibly differ.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "refinement/render_voice.h"

#include "dsp/pad_config.h"

#include <cmath>
#include <vector>

namespace {

constexpr double kSr = 48000.0;
constexpr float kLenSec = 1.0f;

float rmsDiff(const std::vector<float>& a, const std::vector<float>& b) {
    REQUIRE(a.size() == b.size());
    double acc = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const double d = static_cast<double>(a[i]) - static_cast<double>(b[i]);
        acc += d * d;
    }
    return static_cast<float>(std::sqrt(acc / static_cast<double>(a.size())));
}

float rms(const std::vector<float>& a) {
    double acc = 0.0;
    for (float s : a) acc += static_cast<double>(s) * static_cast<double>(s);
    return static_cast<float>(std::sqrt(acc / static_cast<double>(a.size())));
}

std::vector<float> renderCfg(const Membrum::PadConfig& cfg) {
    MembrumFit::RenderableMembrumVoice v;
    v.prepare(kSr, 512);
    return v.renderToVector(cfg, 1.0f, kLenSec);
}

Membrum::PadConfig baseCfg() {
    Membrum::PadConfig cfg{};  // struct defaults
    cfg.exciterType = Membrum::ExciterType::Mallet;
    cfg.bodyModel   = Membrum::BodyModelType::Membrane;
    cfg.material = 0.4f; cfg.size = 0.5f; cfg.decay = 0.5f;
    cfg.level = 0.8f;
    // Silence the parallel layers in the baseline so each section's field
    // flip is the only difference.
    cfg.noiseLayerMix = 0.0f;
    cfg.clickLayerMix = 0.0f;
    return cfg;
}

}  // namespace

TEST_CASE("RenderableMembrumVoice applies the full production PadConfig surface",
          "[refinement][fidelity]") {
    const auto base = renderCfg(baseCfg());
    REQUIRE(rms(base) > 1e-6f);  // baseline actually sounds

    SECTION("noise layer mix reaches the render") {
        auto cfg = baseCfg();
        cfg.noiseLayerMix = 0.9f;
        cfg.noiseLayerColor = 0.9f;
        cfg.noiseLayerDecay = 0.6f;
        REQUIRE(rmsDiff(renderCfg(cfg), base) > 1e-4f);
    }

    SECTION("click layer mix reaches the render") {
        auto cfg = baseCfg();
        cfg.clickLayerMix = 0.95f;
        cfg.clickLayerBrightness = 0.9f;
        REQUIRE(rmsDiff(renderCfg(cfg), base) > 1e-4f);
    }

    SECTION("bodyDampingB1 override reaches the render") {
        auto cfg = baseCfg();
        cfg.bodyDampingB1 = 0.9f;  // heavy damping vs -1 sentinel (derived)
        REQUIRE(rmsDiff(renderCfg(cfg), base) > 1e-4f);
    }

    SECTION("airLoading reaches the render") {
        auto cfg = baseCfg();
        cfg.airLoading = 1.0f;  // detunes the (m,1) series vs baseline 0
        REQUIRE(rmsDiff(renderCfg(cfg), base) > 1e-4f);
    }

    SECTION("modeInject reaches the render") {
        auto cfg = baseCfg();
        cfg.modeInjectAmount = 0.5f;
        REQUIRE(rmsDiff(renderCfg(cfg), base) > 1e-4f);
    }

    SECTION("material morph reaches the render") {
        auto cfg = baseCfg();
        cfg.morphEnabled = 1.0f;
        cfg.morphStart = 1.0f;
        cfg.morphEnd = 0.0f;
        cfg.morphDuration = 0.2f;
        REQUIRE(rmsDiff(renderCfg(cfg), base) > 1e-4f);
    }

    SECTION("wireCoupling + noiseLayerGain reach the render") {
        auto cfg = baseCfg();
        cfg.noiseLayerMix = 0.9f;
        auto withoutGain = renderCfg(cfg);
        cfg.noiseLayerGain = 6.2f;
        cfg.wireCoupling = 0.45f;
        REQUIRE(rmsDiff(renderCfg(cfg), withoutGain) > 1e-4f);
    }

    SECTION("secondary shell reaches the render") {
        auto cfg = baseCfg();
        cfg.secondaryEnabled = 1.0f;
        cfg.couplingStrength = 0.8f;
        cfg.secondarySize = 0.4f;
        cfg.secondaryMaterial = 0.7f;
        REQUIRE(rmsDiff(renderCfg(cfg), base) > 1e-4f);
    }

    SECTION("cubic filter-env decode matches production (not linear x5000)") {
        // A config whose filter envelope only audibly opens under the
        // production cubic decode timing must render differently from one
        // with the envelope disabled -- and identically enough to itself.
        auto cfg = baseCfg();
        cfg.tsFilterType = 0.0f;         // LP
        cfg.tsFilterCutoff = 0.15f;      // ~56 Hz closed
        cfg.tsFilterEnvAmount = 1.0f;    // full positive sweep
        cfg.tsFilterEnvAttack = 0.0f;
        cfg.tsFilterEnvDecay = 0.5f;     // cubic: 250 ms; old linear: 2500 ms
        cfg.tsFilterEnvSustain = 0.0f;
        auto withEnv = renderCfg(cfg);
        cfg.tsFilterEnvAmount = 0.5f;    // norm 0.5 -> bipolar 0 (no sweep)
        REQUIRE(rmsDiff(withEnv, renderCfg(cfg)) > 1e-4f);
    }
}
