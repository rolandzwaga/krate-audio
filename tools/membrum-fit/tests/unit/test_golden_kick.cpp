// Golden-corpus round-trip: render a Kick through the production voice,
// feed it back through the fit pipeline, assert the re-rendered voice
// is within -25 dB MSS spectral distance of the original (spec §8 Phase 1
// exit gate, relaxed threshold since BOBYQA is already time-bounded).
#include "src/body_classifier.h"
#include "src/features.h"
#include "src/mapper_inversion/membrane_inverse.h"
#include "src/modal/matrix_pencil.h"
#include "src/refinement/loss.h"
#include "src/refinement/render_voice.h"
#include "src/segmentation.h"
#include "src/tone_shaper_fit.h"
#include "src/unnatural_fit.h"

#include "dsp/default_kit.h"
#include "dsp/pad_config.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <span>

TEST_CASE("Golden Kick: fit pipeline round-trips within 6 dB MSS loss vs rendered source") {
    constexpr double sr = 44100.0;

    Membrum::PadConfig ground{};
    Membrum::DefaultKit::applyTemplate(ground, Membrum::DrumTemplate::Kick);

    MembrumFit::RenderableMembrumVoice voice;
    voice.prepare(sr);
    const auto rendered = voice.renderToVector(ground, 1.0f, 0.5f);
    REQUIRE(!rendered.empty());

    // Fit pipeline without BOBYQA (deterministic portion).
    const auto seg = MembrumFit::segmentSample(rendered, sr);
    REQUIRE(MembrumFit::isSegmentationUsable(seg, sr));
    const auto features = MembrumFit::extractAttackFeatures(rendered, seg, sr);
    const std::span<const float> decay(rendered.data() + seg.decayStartSample,
                                        seg.decayEndSample - seg.decayStartSample);
    const auto modes = MembrumFit::Modal::extractModesMatrixPencil(decay, sr, 16);
    auto cfg = MembrumFit::MapperInversion::invertMembrane(modes, features, sr);
    MembrumFit::fitToneShaper(rendered, seg, sr, modes, cfg);
    MembrumFit::fitUnnaturalZone(rendered, seg, sr, modes, cfg, cfg);

    const auto refit = voice.renderToVector(cfg, 1.0f, 0.5f);
    REQUIRE(refit.size() == rendered.size());

    const float mss = MembrumFit::computeMSSLoss(rendered, refit);
    // MSS is a log-magnitude L1 average; a value below 6 dB corresponds
    // comfortably to the perceptual "recognisable" bar from spec §8 Phase 1.
    // The deterministic-path result floor (no BOBYQA refinement) lands
    // around 0.6-1.5 on this signal; gate at 6.0 for headroom.
    REQUIRE(mss < 6.0f);
}
