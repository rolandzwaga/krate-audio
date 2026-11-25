// ==============================================================================
// One voice renderer (fix plan T2.1: MB-041)
// ==============================================================================
// VoicePool renders every voice through DrumVoice::processBlock. process(), the
// per-sample entry point the spec acceptance tests and the phase-1 golden used,
// was a separate renderer: no secondary shell bank, per-sample bank smoothing
// and no retirement, so those tests pinned a path no host runs. process() is
// now processBlock(&s, 1); these cases hold the two entry points to the same
// note and guard the block-size independence the wrapper relies on (T1.11).
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "dsp/body_model_type.h"
#include "dsp/drum_voice.h"
#include "dsp/exciter_type.h"
#include "dsp/tone_shaper.h"

#include "render_fingerprint.h"

#include <krate/dsp/core/db_utils.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace {

using Krate::DSP::TestUtils::compareFingerprints;
using Krate::DSP::TestUtils::fingerprintRender;

constexpr double kSampleRate = 48000.0;
constexpr float  kVelocity   = 100.0f / 127.0f;

// T1.11's block-size identity bound (test_pitch_env_block_path.cpp, (d)).
constexpr double kBlockSizeMaxAbsDiff = 3.0e-5;

// Impulse + Membrane with every block-path-only stage engaged: the secondary
// shell bank (enabled AND a coupling strength above 0, drum_voice.h noteOn),
// tension modulation, wire coupling into an audible noise layer, and a
// 160 -> 50 Hz pitch envelope over 20 ms (the SC-009 sweep).
std::unique_ptr<Membrum::DrumVoice> makeVoice()
{
    auto v = std::make_unique<Membrum::DrumVoice>();
    v->prepare(kSampleRate, 0u);
    v->setMaterial(0.5f);
    v->setSize(0.5f);
    v->setDecay(0.5f);
    v->setStrikePosition(0.3f);
    v->setLevel(0.8f);
    v->setExciterType(Membrum::ExciterType::Impulse);
    v->setBodyModel(Membrum::BodyModelType::Membrane);
    v->setSecondaryEnabled(1.0f);
    v->setCouplingStrength(0.5f);
    v->setTensionModAmt(0.8f);
    v->setWireCoupling(1.0f);
    v->setNoiseLayerMix(0.5f);
    v->setNoiseLayerGain(4.0f);
    v->toneShaper().setPitchEnvStartHz(160.0f);
    v->toneShaper().setPitchEnvEndHz(50.0f);
    v->toneShaper().setPitchEnvTimeMs(20.0f);
    v->toneShaper().setPitchEnvCurve(Membrum::ToneShaperCurve::Exponential);
    return v;
}

std::vector<float> renderPerSample(Membrum::DrumVoice& v, std::size_t total)
{
    std::vector<float> out(total, 0.0f);
    for (std::size_t i = 0; i < total; ++i)
        out[i] = v.process();
    return out;
}

std::vector<float> renderBlocks(Membrum::DrumVoice& v, std::size_t total, int blockSize)
{
    std::vector<float> out(total, 0.0f);
    std::size_t pos = 0;
    while (pos < total)
    {
        const auto n = std::min(static_cast<std::size_t>(blockSize), total - pos);
        v.processBlock(out.data() + pos, static_cast<int>(n));
        pos += n;
    }
    return out;
}

bool allFinite(const std::vector<float>& x)
{
    return std::all_of(x.begin(), x.end(),
                       [](float s) { return Krate::DSP::detail::isFinite(s); });
}

double maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b)
{
    const std::size_t n = std::min(a.size(), b.size());
    double d = 0.0;
    for (std::size_t i = 0; i < n; ++i)
        d = std::max(d, static_cast<double>(std::fabs(a[i] - b[i])));
    return d;
}

} // namespace

TEST_CASE("DrumVoice: process() renders the same note as processBlock (shell, tension, wire coupling, pitch envelope)",
          "[drum_voice][renderer]")
{
    const auto total = static_cast<std::size_t>(kSampleRate * 1.0);

    auto perSample = makeVoice();
    perSample->noteOn(kVelocity);
    const std::vector<float> a = renderPerSample(*perSample, total);

    auto block = makeVoice();
    block->noteOn(kVelocity);
    const std::vector<float> b = renderBlocks(*block, total, 64);

    REQUIRE(allFinite(a));
    REQUIRE(allFinite(b));

    const auto cmp = compareFingerprints(fingerprintRender(std::span<const float>(a)),
                                         fingerprintRender(std::span<const float>(b)));
    INFO(cmp.detail);
    INFO("worst metric rel err " << cmp.worstMetricRelativeError
         << ", worst checkpoint err " << cmp.worstSampleError
         << ", max |diff| " << maxAbsDiff(a, b));
    CHECK(cmp.withinTolerance());
}

TEST_CASE("DrumVoice: a note rendered through process() retires", "[drum_voice][renderer]")
{
    auto v = std::make_unique<Membrum::DrumVoice>();
    v->prepare(kSampleRate, 0u);
    v->noteOn(kVelocity);
    REQUIRE(v->isActive());

    const auto total = static_cast<std::size_t>(kSampleRate * 20.0);
    bool finite = true;
    for (std::size_t i = 0; i < total; ++i)
        finite = finite && Krate::DSP::detail::isFinite(v->process());

    CHECK(finite);
    CHECK_FALSE(v->isActive());
}

TEST_CASE("DrumVoice: processBlock renders the same note at block sizes 1, 64 and 2048",
          "[drum_voice][renderer]")
{
    const auto total = static_cast<std::size_t>(kSampleRate * 1.0);

    auto ref = makeVoice();
    ref->noteOn(kVelocity);
    const std::vector<float> r64 = renderBlocks(*ref, total, 64);
    REQUIRE(allFinite(r64));
    const auto fp64 = fingerprintRender(std::span<const float>(r64));

    for (int bs : {1, 2048})
    {
        auto v = makeVoice();
        v->noteOn(kVelocity);
        const std::vector<float> r = renderBlocks(*v, total, bs);
        REQUIRE(allFinite(r));

        const auto cmp = compareFingerprints(fingerprintRender(std::span<const float>(r)), fp64);
        const double d = maxAbsDiff(r, r64);
        INFO("block=" << bs << ": " << cmp.detail);
        INFO("block=" << bs << " worst metric rel err " << cmp.worstMetricRelativeError
             << ", worst checkpoint err " << cmp.worstSampleError
             << ", max |diff| vs block 64 = " << d);
        CHECK(cmp.withinTolerance());
        CHECK(d <= kBlockSizeMaxAbsDiff);
    }
}
