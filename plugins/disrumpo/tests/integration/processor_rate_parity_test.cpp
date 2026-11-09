// =============================================================================
// Processor vs DistortionAdapter parity for every type (fix plan T6.02)
// =============================================================================
// Promoted from the audit probe "adapter-vs-processor" (specs/disrumpo-audit/probes/
// audit_probes.cpp.txt, probeType). The same node (type, shape-slot params, drive 7.5,
// mix 1, default tone) rendered through the Processor (one band, left channel) and
// through a lone DistortionAdapter must read the same level (|RMS diff| <= 1 dB) and the
// same brightness (spectral centroid ratio 0.85-1.15).
//
// The band runs its node at the type's oversampling factor (1x, 2x or 4x), so the
// reference adapter is prepared at that clocked rate and fed the tone generated at it,
// and both are read below 18 kHz (the downsampler removes the rest). An adapter at the
// host rate would differ by its own aliasing: at the default Oversample Max the centroid
// ratio reads 0.71 (RingSaturation) to 0.87, which is the oversampling working, not a
// rate error.
//
// Audit baseline (2026-11-05, reference at 48 kHz): 19 bad, 6 suspicious, 1 ok -
// SergeFold -11.9 dB / 0.077, FeedbackDist +9.9 dB / 0.282, RingSaturation +5.9 dB /
// 0.359, Bitcrush -3.01 dB / 1.000 (the adapters ran at 8x their prepared rate, L and R
// through one engine, and the centre-pan law cost 3 dB: D-009, D-016, D-021).
// =============================================================================

#include "../test_support/disrumpo_rig.h"

#include "dsp/distortion_adapter.h"
#include "dsp/distortion_types.h"
#include "plugin_ids.h"

#include <krate/dsp/core/window_functions.h>
#include <krate/dsp/primitives/fft.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <numbers>
#include <vector>

using namespace Disrumpo;
using namespace Disrumpo::TestSupport;

namespace {

constexpr double kSr = 48000.0;
constexpr double kToneHz = 1031.25;
constexpr size_t kWarm = 24000;
constexpr size_t kMeasure = 8192;
constexpr size_t kLen = kWarm + kMeasure;
constexpr double kMaxHz = 18000.0;

std::vector<float> toneAtMinus6(size_t n, double sr) {
    const double a = std::pow(10.0, -6.0 / 20.0);
    std::vector<float> x(n);
    for (size_t i = 0; i < n; ++i) {
        x[i] = static_cast<float>(a * std::sin(2.0 * std::numbers::pi * kToneHz * static_cast<double>(i) / sr));
    }
    return x;
}

double typeNorm(DistortionType t) { return static_cast<double>(static_cast<int>(t)) / 25.0; }

struct Spectrum {
    double meanSquare = 0.0;  ///< mean-square level of the content below kMaxHz
    double centroidHz = 0.0;  ///< magnitude-weighted centroid below kMaxHz
};

/// Level and centroid of `x` (rate `sr`) below kMaxHz: Hann frames of 2048 samples per
/// 48 kHz (every rate gets the same 23.4 Hz bins), averaged over the whole signal.
Spectrum bandLimited(const std::vector<float>& x, double sr) {
    const auto n = static_cast<size_t>(2048.0 * sr / kSr);
    Krate::DSP::FFT fft;
    fft.prepare(n);
    std::vector<float> window(n);
    Krate::DSP::Window::generateHann(window.data(), n);
    double windowPower = 0.0;
    for (const float w : window) windowPower += static_cast<double>(w) * static_cast<double>(w);
    std::vector<float> frame(n);
    std::vector<Krate::DSP::Complex> spectrum(fft.numBins());
    double power = 0.0;
    double weighted = 0.0;
    double magnitude = 0.0;
    size_t frames = 0;
    for (size_t start = 0; start + n <= x.size(); start += n, ++frames) {
        for (size_t i = 0; i < n; ++i) frame[i] = x[start + i] * window[i];
        fft.forward(frame.data(), spectrum.data());
        for (size_t k = 1; k < spectrum.size(); ++k) {
            const double hz = static_cast<double>(k) * sr / static_cast<double>(n);
            if (hz > kMaxHz) break;
            const double m = spectrum[k].magnitude();
            power += 2.0 * m * m;
            weighted += m * hz;
            magnitude += m;
        }
    }
    return {.meanSquare = power / (static_cast<double>(frames) * static_cast<double>(n) * windowPower),
            .centroidHz = magnitude > 0.0 ? weighted / magnitude : 0.0};
}

struct Parity {
    int factor = 1;              ///< the band's oversampling factor
    double rmsDiffDb = 0.0;      ///< processor - adapter, below kMaxHz
    double centroidRatio = 0.0;  ///< processor / adapter, below kMaxHz
    double adapterCentroidHz = 0.0;
    double processorCentroidHz = 0.0;
};

Parity measureParity(DistortionType type) {
    Rig r(kSr);
    PList params{{kBandCountId, 0.0},
                 {makeNodeParamId(0, 0, NodeParamType::kNodeType), typeNorm(type)},
                 {makeNodeParamId(0, 0, NodeParamType::kNodeDrive), 0.75},
                 {makeNodeParamId(0, 0, NodeParamType::kNodeMix), 1.0},
                 {makeNodeParamId(0, 0, NodeParamType::kNodeBias), 0.5}};
    // RingSaturation's default carrier (Track) locks to the input with a phase set by when
    // its tracker first locks, so its level depends on that phase (2.3 dB here at 4x), not
    // on the rate. The Fixed carrier (frequency mode slot 0) has no such dependence.
    if (type == DistortionType::RingSaturation) {
        params.emplace_back(makeNodeParamId(0, 0, NodeParamType::kNodeShape5), 0.0);
    }
    r.apply(params);
    r.reactivate();
    const auto in = toneAtMinus6(kLen, kSr);
    std::vector<float> outL;
    std::vector<float> outR;
    r.render(in, in, outL, outR);
    const std::vector<float> proc(outL.begin() + static_cast<std::ptrdiff_t>(kWarm), outL.end());
    const int factor = r.p->getBandProcessor(0).getOversampleFactor();

    // The same node, as the band runs it: an adapter prepared at the clocked rate. The type
    // parameters are set before the common ones, as MorphEngine::setNodes does.
    const MorphEngine* engine = r.p->getBandProcessor(0).getMorphEngine();
    REQUIRE(engine != nullptr);
    const double rate = kSr * static_cast<double>(factor);
    auto adapter = std::make_unique<DistortionAdapter>();
    adapter->prepare(rate, 512 * factor);
    adapter->setType(type);
    adapter->setParams(engine->getNodeParams(0));
    adapter->setCommonParams(engine->getNodeCommonParams(0));
    const auto warm = kWarm * static_cast<size_t>(factor);
    const auto at = toneAtMinus6(kLen * static_cast<size_t>(factor), rate);
    std::vector<float> ref(at.size() - warm);
    for (size_t i = 0; i < at.size(); ++i) {
        const float y = adapter->process(at[i]);
        if (i >= warm) ref[i - warm] = y;
    }

    const auto sp = bandLimited(proc, kSr);
    const auto sa = bandLimited(ref, rate);
    return {.factor = factor,
            .rmsDiffDb = 10.0 * std::log10((sp.meanSquare + 1e-30) / (sa.meanSquare + 1e-30)),
            .centroidRatio = sa.centroidHz > 0.0 ? sp.centroidHz / sa.centroidHz : 0.0,
            .adapterCentroidHz = sa.centroidHz,
            .processorCentroidHz = sp.centroidHz};
}

}  // namespace

TEST_CASE("Processor renders every type like a DistortionAdapter at the band's clocked rate",
          "[disrumpo][audit][rate-parity][D-009][D-016]") {
    for (int t = 0; t < kDistortionTypeCount; ++t) {
        const auto type = static_cast<DistortionType>(t);
        const auto p = measureParity(type);
        std::printf("T6.02 %s at %dx: RMS processor - adapter %.2f dB, centroid ratio %.3f (adapter %.0f Hz, "
                    "processor %.0f Hz)\n", getTypeName(type), p.factor, p.rmsDiffDb, p.centroidRatio,
                    p.adapterCentroidHz, p.processorCentroidHz);
        INFO(getTypeName(type) << " at " << p.factor << "x: RMS diff " << p.rmsDiffDb << " dB, centroid ratio "
             << p.centroidRatio);
        CHECK(std::fabs(p.rmsDiffDb) <= 1.0);
        CHECK(p.centroidRatio >= 0.85);
        CHECK(p.centroidRatio <= 1.15);
    }
}
