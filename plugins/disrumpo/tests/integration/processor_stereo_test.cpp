// =============================================================================
// Processor stereo independence through a morph band
// =============================================================================
// Audit findings D-016 / D-119 (fix plan T1.05): BandProcessor ran L and then R
// through ONE MorphEngine (and one legacy DistortionAdapter), so every stateful
// element inside it (tone filter, DC blocker, tape filters, feedback delay line,
// formant filters, cursor smoothers) was clocked with an interleaved L,R,L,R
// stream. A signal on L leaked into R at about -14 dBFS. Each channel now has
// its own engine and adapter.
// =============================================================================

#include "../test_support/disrumpo_rig.h"

#include "plugin_ids.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

using namespace Disrumpo;
using namespace Disrumpo::TestSupport;

namespace {

constexpr double kSr = 48000.0;
constexpr double kToneHz = 1031.0;

std::vector<float> sine(size_t n, double f, double sr, double dbfs) {
    const double a = std::pow(10.0, dbfs / 20.0);
    std::vector<float> x(n);
    for (size_t i = 0; i < n; ++i) {
        x[i] = static_cast<float>(a * std::sin(2.0 * std::numbers::pi * f * static_cast<double>(i) / sr));
    }
    return x;
}

std::vector<float> negated(const std::vector<float>& x) {
    std::vector<float> y(x.size());
    std::transform(x.begin(), x.end(), y.begin(), [](float v) { return -v; });
    return y;
}

ParamID nodeId(NodeParamType t) { return makeNodeParamId(0, 0, t); }

double typeNorm(DistortionType t) { return static_cast<double>(static_cast<int>(t)) / 25.0; }

/// One band, node 0 of the given type, drive 0.5 (normalized), mix 1, plus `extra`.
void renderOneBand(DistortionType type, const std::vector<float>& inL, const std::vector<float>& inR,
                   std::vector<float>& outL, std::vector<float>& outR, const PList& extra = {}) {
    Rig r(kSr);
    PList params{{kBandCountId, 0.0},
                 {nodeId(NodeParamType::kNodeType), typeNorm(type)},
                 {nodeId(NodeParamType::kNodeDrive), 0.5},
                 {nodeId(NodeParamType::kNodeMix), 1.0}};
    params.insert(params.end(), extra.begin(), extra.end());
    r.apply(params);
    r.reactivate();
    r.render(inL, inR, outL, outR);
}

/// RMS in dBFS of R over the last 0.5 s of a 1 s render when only L carries a signal.
double leakIntoRightDb(DistortionType type, double& leftDb) {
    const auto len = static_cast<size_t>(kSr);
    const auto inL = sine(len, kToneHz, kSr, -6.0);
    const std::vector<float> inR(len, 0.0f);
    std::vector<float> outL;
    std::vector<float> outR;
    renderOneBand(type, inL, inR, outL, outR);
    const size_t start = len / 2;
    leftDb = toDb(rmsOf(outL.data() + start, len - start));
    return toDb(rmsOf(outR.data() + start, len - start));
}

double maxAbs(const std::vector<float>& a, const std::vector<float>& b, double signB) {
    double m = 0.0;
    for (size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
        m = std::max(m, std::fabs(static_cast<double>(a[i]) + signB * static_cast<double>(b[i])));
    }
    return m;
}

}  // namespace

TEST_CASE("Processor keeps L and R independent through a morph band", "[disrumpo][stereo][D-016]") {
    const auto len = static_cast<size_t>(kSr);
    const auto tone = sine(len, kToneHz, kSr, -6.0);

    SECTION("Tape: L only, R stays silent") {
        double leftDb = 0.0;
        const double rightDb = leakIntoRightDb(DistortionType::Tape, leftDb);
        INFO("Tape L = " << leftDb << " dBFS, R = " << rightDb << " dBFS");
        CHECK(leftDb > -40.0);
        CHECK(rightDb < -90.0);
    }

    // The default Tape model (shape slot 3 = 0.5 -> Hysteresis) is not an odd
    // function, so f(x) + f(-x) != 0 even with two independent, identical
    // channels. The anti-phase check therefore runs the Simple (tanh) model with
    // bias 0, and the default model is checked with identical L/R input.
    SECTION("Tape (Simple model): anti-phase input gives anti-phase output") {
        std::vector<float> outL;
        std::vector<float> outR;
        renderOneBand(DistortionType::Tape, tone, negated(tone), outL, outR,
                      {{nodeId(NodeParamType::kNodeShape3), 0.0}});
        const double maxSum = maxAbs(outL, outR, 1.0);
        INFO("Tape (Simple) max|L + R| = " << maxSum << ", L rms = " << toDb(rmsOf(outL.data(), len)) << " dBFS");
        CHECK(rmsOf(outL.data(), len) > 0.01);
        CHECK(maxSum < 1e-4);
    }

    SECTION("Tape (default model): identical input gives identical output") {
        std::vector<float> outL;
        std::vector<float> outR;
        renderOneBand(DistortionType::Tape, tone, tone, outL, outR);
        const double maxDiff = maxAbs(outL, outR, -1.0);
        INFO("Tape max|L - R| = " << maxDiff << ", L rms = " << toDb(rmsOf(outL.data(), len)) << " dBFS");
        CHECK(rmsOf(outL.data(), len) > 0.01);
        CHECK(maxDiff < 1e-4);
    }

    SECTION("FeedbackDist: L only, R stays silent") {
        double leftDb = 0.0;
        const double rightDb = leakIntoRightDb(DistortionType::FeedbackDist, leftDb);
        INFO("FeedbackDist L = " << leftDb << " dBFS, R = " << rightDb << " dBFS");
        CHECK(leftDb > -40.0);
        CHECK(rightDb < -90.0);
    }

    SECTION("Formant: L only, R stays silent") {
        double leftDb = 0.0;
        const double rightDb = leakIntoRightDb(DistortionType::Formant, leftDb);
        INFO("Formant L = " << leftDb << " dBFS, R = " << rightDb << " dBFS");
        CHECK(leftDb > -40.0);
        CHECK(rightDb < -90.0);
    }
}
