// =============================================================================
// Per-type render matrix through the Processor (fix plan T6.01)
// =============================================================================
// Promoted from the audit probe "per-type A-D" (specs/disrumpo-audit/probes/
// audit_probes.cpp.txt, probeType). Every distortion type as node 0 of one band,
// 1031.25 Hz at -6 dBFS (bin 176 of an 8192-point FFT at 48 kHz, so aliases do not
// fold onto harmonics), mix 1, at drive 2.5 / 7.5 / 10:
// - no non-finite sample;
// - non-harmonic energy <= -50 dB re the harmonics for the types whose recommended
//   oversampling factor is 2 or 4 (the 1x types alias by design; Stochastic, Granular,
//   Chaos and Spectral add noise by design);
// - peak <= +6 dBFS; |DC| < 1e-3 at bias 0, and at bias -1 / +1 for the types the
//   adapter DC-blocks (DC read over 1.02 s, 1056 whole cycles: a 0.17 s window reads
//   Stochastic's low-frequency noise as -0.0016 of "DC");
// - self-noise of a fresh instance on silence <= -90 dBFS;
// - tail 1.0-1.5 s after the input stops <= -90 dBFS, except the types that ring by
//   design (FeedbackDist, AllpassResonant: feedback > 0 at their defaults), which must
//   fall below -60 dBFS within 5 s.
// Ruled exemptions (2026-11-09, one bar each, see inharmonicByDesign() and below):
// RingSaturation from the non-harmonic bar, Stochastic from the DC bars, Quantize from the
// -90 dBFS bars on silence.
// Audit baseline (2026-11-05): HardClip -43.3, SineFold -3.1, Tape -23.6, Fractal -24.2 dB
// non-harmonic; Aliasing +11.1 dBFS peak; Tube DC -0.041; Stochastic -10.4 and Quantize
// -49.0 dBFS self-noise; FeedbackDist tail -47.9 dBFS.
// =============================================================================

#include "../test_support/audit_metrics.h"
#include "../test_support/disrumpo_rig.h"

#include "dsp/distortion_types.h"
#include "plugin_ids.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <numbers>
#include <vector>

using namespace Disrumpo;
using namespace Disrumpo::TestSupport;

namespace {

constexpr double kSr = 48000.0;
constexpr float kToneHz = 1031.25f;
constexpr size_t kFft = 8192;
constexpr size_t kWarm = 24000;
constexpr size_t kMeasure = 6 * kFft;  // 1.02 s: 1056 whole cycles of the tone
constexpr size_t kLen = kWarm + kMeasure;

std::vector<float> toneAtMinus6(size_t n) {
    const double a = std::pow(10.0, -6.0 / 20.0);
    std::vector<float> x(n);
    for (size_t i = 0; i < n; ++i) {
        x[i] = static_cast<float>(a * std::sin(2.0 * std::numbers::pi * static_cast<double>(kToneHz) *
                                               static_cast<double>(i) / kSr));
    }
    return x;
}

double typeNorm(DistortionType t) { return static_cast<double>(static_cast<int>(t)) / 25.0; }

ParamID node0(NodeParamType t) { return makeNodeParamId(0, 0, t); }

bool noiseByDesign(DistortionType t) {
    return t == DistortionType::Stochastic || t == DistortionType::Granular || t == DistortionType::Chaos ||
           t == DistortionType::Spectral;
}

/// The types DistortionAdapter::updateDCBlockerState() DC-blocks at their defaults.
bool dcBlocked(DistortionType t) {
    switch (t) {
        case DistortionType::AsymmetricFuzz:
        case DistortionType::SineFold:
        case DistortionType::TriangleFold:
        case DistortionType::SergeFold:
        case DistortionType::Chaos:
        case DistortionType::Granular:
        case DistortionType::FullRectify:
        case DistortionType::HalfRectify:
        case DistortionType::FeedbackDist:
        case DistortionType::Tube:
        case DistortionType::Stochastic:
        case DistortionType::Quantize:
            return true;
        default:
            return false;
    }
}

/// Types whose feedback is above 0 at their defaults: their tail rings by design.
bool ringsByDesign(DistortionType t) {
    return t == DistortionType::FeedbackDist || t == DistortionType::AllpassResonant;
}

// User ruling 2026-11-09 (follow-up 79): three types are exempt from one bar each as
// intended behaviour; every other bar still applies to them.

/// Ring modulation is inharmonic by design: non-harmonic -33.6 / -29.4 / -30.2 dB at drive
/// 2.5 / 7.5 / 10 (Fixed carrier -0.7 dB, ring depth 0 -129.6 dB). No -50 dB bar.
bool inharmonicByDesign(DistortionType t) { return t == DistortionType::RingSaturation; }

/// Stochastic's slow random drift sits below the adapter's 10 Hz DC blocker: |DC| 0.00253
/// over 1.02 s at drive 7.5 / 10 and at bias -1 / +1 (0.00042 at drive 2.5). No DC bar.
bool driftsBelowDcBlocker(DistortionType t) { return t == DistortionType::Stochastic; }

/// Quantize's dither keeps running on silence (follow-up 39): self-noise -40.5 dBFS, tail
/// -40.7 dBFS (both read the dither on digital silence). No -90 dBFS bar on silence.
bool dithersOnSilence(DistortionType t) { return t == DistortionType::Quantize; }

size_t countNonFinite(const std::vector<float>& a, const std::vector<float>& b) {
    size_t c = 0;
    for (const float v : a) c += fin(v) ? 0 : 1;
    for (const float v : b) c += fin(v) ? 0 : 1;
    return c;
}

/// One band, node 0 of `type`, mix 1, bias 0 (normalized 0.5).
void setUpType(Rig& r, DistortionType type) {
    r.apply({{kBandCountId, 0.0},
             {node0(NodeParamType::kNodeType), typeNorm(type)},
             {node0(NodeParamType::kNodeMix), 1.0},
             {node0(NodeParamType::kNodeBias), 0.5}});
}

void checkType(DistortionType type) {
    const char* name = getTypeName(type);
    const int osProfile = getRecommendedOversample(type);
    const auto in = toneAtMinus6(kLen);
    std::vector<float> outL;
    std::vector<float> outR;

    Rig r(kSr);
    setUpType(r, type);
    for (const double drive : {0.25, 0.75, 1.0}) {
        r.apply({{node0(NodeParamType::kNodeDrive), drive}});
        r.reactivate();
        r.render(in, in, outL, outR);
        const size_t nonFinite = countNonFinite(outL, outR);
        const double nonHarmonicDb = readAliasing(outL, kSr, kToneHz, kFft, 4).nonHarmonicRelDb();
        const double dc = meanOf(outL.data() + kWarm, kMeasure);
        const double peakDb = toDb(peakOf(outL.data() + kWarm, kMeasure));
        std::printf("T6.01 %s drive %.1f: non-harmonic %.1f dB (OS profile %dx), DC %.3g, peak %.2f dBFS, "
                    "non-finite %zu\n", name, drive * 10.0, nonHarmonicDb, osProfile, dc, peakDb, nonFinite);
        INFO(name << " drive " << drive * 10.0 << ": non-harmonic " << nonHarmonicDb << " dB, DC " << dc << ", peak "
             << peakDb << " dBFS");
        CHECK(nonFinite == 0);
        if (osProfile >= 2 && !noiseByDesign(type) && !inharmonicByDesign(type)) CHECK(nonHarmonicDb <= -50.0);
        CHECK(peakDb <= 6.0);
        if (!driftsBelowDcBlocker(type)) CHECK(std::fabs(dc) < 1e-3);

        if (drive == 1.0) {
            // The input stops: 1.5 s of silence (5 s for the ringing types)
            const auto tailLen = static_cast<size_t>(kSr * (ringsByDesign(type) ? 5.0 : 1.5));
            const std::vector<float> silence(tailLen, 0.0f);
            std::vector<float> tL;
            std::vector<float> tR;
            r.render(silence, silence, tL, tR);
            const auto from = static_cast<size_t>(kSr);
            const auto to = static_cast<size_t>(kSr * 1.5);
            const double tailDb = toDb(std::max(rmsOf(tL.data() + from, to - from), rmsOf(tR.data() + from, to - from)));
            const size_t endFrom = tailLen - static_cast<size_t>(kSr / 2.0);
            const double endDb = toDb(std::max(rmsOf(tL.data() + endFrom, tailLen - endFrom),
                                               rmsOf(tR.data() + endFrom, tailLen - endFrom)));
            std::printf("T6.01 %s tail: %.1f dBFS 1.0-1.5 s after the input stops, %.1f dBFS at %.1f s\n", name, tailDb,
                        endDb, static_cast<double>(tailLen) / kSr);
            INFO(name << " tail " << tailDb << " dBFS (1.0-1.5 s), " << endDb << " dBFS (last 0.5 s)");
            CHECK(countNonFinite(tL, tR) == 0);
            if (ringsByDesign(type)) {
                CHECK(endDb <= -60.0);
            } else if (!dithersOnSilence(type)) {
                CHECK(tailDb <= -90.0);
            }
        }
    }

    // Bias -1 / +1 at drive 7.5
    for (const double bias : {0.0, 1.0}) {
        r.apply({{node0(NodeParamType::kNodeDrive), 0.75}, {node0(NodeParamType::kNodeBias), bias}});
        r.reactivate();
        r.render(in, in, outL, outR);
        const double dc = meanOf(outL.data() + kWarm, kMeasure);
        std::printf("T6.01 %s bias %+.0f: DC %.3g (%s)\n", name, bias * 2.0 - 1.0, dc,
                    dcBlocked(type) ? "DC-blocked" : "not DC-blocked");
        INFO(name << " bias " << bias * 2.0 - 1.0 << ": DC " << dc);
        CHECK(countNonFinite(outL, outR) == 0);
        if (dcBlocked(type) && !driftsBelowDcBlocker(type)) CHECK(std::fabs(dc) < 1e-3);
    }

    // Self-noise: a fresh instance at drive 10 on 1 s of silence, RMS of the last 0.5 s
    Rig fresh(kSr);
    setUpType(fresh, type);
    fresh.apply({{node0(NodeParamType::kNodeDrive), 1.0}});
    fresh.reactivate();
    const std::vector<float> silence(static_cast<size_t>(kSr), 0.0f);
    fresh.render(silence, silence, outL, outR);
    const size_t from = outL.size() / 2;
    const double noiseDb =
        toDb(std::max(rmsOf(outL.data() + from, outL.size() - from), rmsOf(outR.data() + from, outR.size() - from)));
    std::printf("T6.01 %s self-noise: %.1f dBFS\n", name, noiseDb);
    INFO(name << " self-noise " << noiseDb << " dBFS");
    CHECK(countNonFinite(outL, outR) == 0);
    if (!dithersOnSilence(type)) CHECK(noiseDb <= -90.0);
}

}  // namespace

// One case per type (in DistortionType order), so a red names its type and the gate shards them.
TEST_CASE("Type render matrix: SoftClip", "[disrumpo][audit][type-matrix]") { checkType(DistortionType::SoftClip); }
TEST_CASE("Type render matrix: HardClip", "[disrumpo][audit][type-matrix]") { checkType(DistortionType::HardClip); }
TEST_CASE("Type render matrix: Tube", "[disrumpo][audit][type-matrix]") { checkType(DistortionType::Tube); }
TEST_CASE("Type render matrix: Tape", "[disrumpo][audit][type-matrix]") { checkType(DistortionType::Tape); }
TEST_CASE("Type render matrix: Fuzz", "[disrumpo][audit][type-matrix]") { checkType(DistortionType::Fuzz); }
TEST_CASE("Type render matrix: AsymmetricFuzz", "[disrumpo][audit][type-matrix]") {
    checkType(DistortionType::AsymmetricFuzz);
}
TEST_CASE("Type render matrix: SineFold", "[disrumpo][audit][type-matrix]") { checkType(DistortionType::SineFold); }
TEST_CASE("Type render matrix: TriangleFold", "[disrumpo][audit][type-matrix]") {
    checkType(DistortionType::TriangleFold);
}
TEST_CASE("Type render matrix: SergeFold", "[disrumpo][audit][type-matrix]") { checkType(DistortionType::SergeFold); }
TEST_CASE("Type render matrix: FullRectify", "[disrumpo][audit][type-matrix]") {
    checkType(DistortionType::FullRectify);
}
TEST_CASE("Type render matrix: HalfRectify", "[disrumpo][audit][type-matrix]") {
    checkType(DistortionType::HalfRectify);
}
TEST_CASE("Type render matrix: Bitcrush", "[disrumpo][audit][type-matrix]") { checkType(DistortionType::Bitcrush); }
TEST_CASE("Type render matrix: SampleReduce", "[disrumpo][audit][type-matrix]") {
    checkType(DistortionType::SampleReduce);
}
TEST_CASE("Type render matrix: Quantize", "[disrumpo][audit][type-matrix]") { checkType(DistortionType::Quantize); }
TEST_CASE("Type render matrix: Aliasing", "[disrumpo][audit][type-matrix]") { checkType(DistortionType::Aliasing); }
TEST_CASE("Type render matrix: BitwiseMangler", "[disrumpo][audit][type-matrix]") {
    checkType(DistortionType::BitwiseMangler);
}
TEST_CASE("Type render matrix: Temporal", "[disrumpo][audit][type-matrix]") { checkType(DistortionType::Temporal); }
TEST_CASE("Type render matrix: RingSaturation", "[disrumpo][audit][type-matrix]") {
    checkType(DistortionType::RingSaturation);
}
TEST_CASE("Type render matrix: FeedbackDist", "[disrumpo][audit][type-matrix]") {
    checkType(DistortionType::FeedbackDist);
}
TEST_CASE("Type render matrix: AllpassResonant", "[disrumpo][audit][type-matrix]") {
    checkType(DistortionType::AllpassResonant);
}
TEST_CASE("Type render matrix: Chaos", "[disrumpo][audit][type-matrix]") { checkType(DistortionType::Chaos); }
TEST_CASE("Type render matrix: Formant", "[disrumpo][audit][type-matrix]") { checkType(DistortionType::Formant); }
TEST_CASE("Type render matrix: Granular", "[disrumpo][audit][type-matrix]") { checkType(DistortionType::Granular); }
TEST_CASE("Type render matrix: Spectral", "[disrumpo][audit][type-matrix]") { checkType(DistortionType::Spectral); }
TEST_CASE("Type render matrix: Stochastic", "[disrumpo][audit][type-matrix]") {
    checkType(DistortionType::Stochastic);
}
