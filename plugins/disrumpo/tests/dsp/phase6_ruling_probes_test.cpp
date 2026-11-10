// =============================================================================
// Hidden probes for the 2026-11-09 rulings on the held Phase 6 cases
// (specs/disrumpo-audit/follow-ups.md items 79 and 81). Not part of any gate.
// =============================================================================
// [p6r-peaks]   peak / RMS of Fuzz, AsymmetricFuzz, FullRectify, HalfRectify at drive
//               1 / 2.5 / 5 / 7.5 / 10 through the Processor (T6.01 signal).
// [p6r-split]   non-harmonic energy of Tape, Fractal, TriangleFold, HalfRectify split into
//               aliasing (falls with the clocked rate / a lower tone) and the rest: the node
//               as an adapter clocked at 48 kHz x N, read below 23.9 kHz (an ideal decimator).
// [p6r-tape]    the bare TapeSaturator: Hysteresis vs Simple, solvers, rate scaling, bias,
//               Sag (the state clamp), input level; flat-top share and odd harmonics.
// [p6r-tape-sag] Tape node Sag 0.5..1 through the Processor at 1x and 2x; the node defaults of
//               HalfRectify, Fractal and TriangleFold.
// [p6r-adaa]    the bare TriangleFold / HalfRectify shapers plain vs first-order ADAA at 4x / 8x.
// [p6r-latency] oversampler group delay vs its impulse-peak delay; dry/wet sum per latency.
// [p6r-arrival] per-band arrival as fractional group delays.
// [p6r-xsum]    crossover sum of a 1x band with a 2x or 4x band against all-1x.
// [p6s-ab-dump] / [p6s-ab-compare] the ADAA ruling's A/B of HalfRectify / TriangleFold.
// [p6d-ringsat] Ring Saturation per-sample carrier / tracker A/B (follow-up 84, 2026-11-10).
// Run one case by name; results of 2026-11-09 are in follow-ups 79 and 81.
// =============================================================================

#include "../test_support/audit_metrics.h"
#include "../test_support/disrumpo_rig.h"

#include "dsp/distortion_adapter.h"
#include "dsp/distortion_types.h"
#include "plugin_ids.h"

#include <krate/dsp/primitives/oversampler.h>
#include <krate/dsp/processors/tape_saturator.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

using namespace Disrumpo;
using namespace Disrumpo::TestSupport;

namespace {

constexpr double kSr = 48000.0;
constexpr size_t kBaseFft = 8192;  // bin width 5.859375 Hz at 48 kHz (and at N x 48 kHz with N x 8192)

std::vector<float> sine(size_t n, double hz, double sr, double amp) {
    std::vector<float> x(n);
    for (size_t i = 0; i < n; ++i) {
        x[i] = static_cast<float>(amp * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / sr));
    }
    return x;
}

double typeNorm(DistortionType t) { return static_cast<double>(static_cast<int>(t)) / 25.0; }
ParamID node0(NodeParamType t) { return makeNodeParamId(0, 0, t); }

void fft(std::vector<std::complex<double>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * std::numbers::pi / static_cast<double>(len);
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k) {
                const auto u = a[i + k];
                const auto v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

struct Split {
    double nonHarmRelDb = 0.0;
    double periodicResidualDb = 0.0;  ///< y[n + period] - y[n] re y (time variance, noise, sub-harmonics)
    std::vector<std::pair<double, double>> top;  ///< strongest non-harmonic bins (Hz, dB re harmonics)
};

/// Non-harmonic power below maxHz re harmonic power: last `frames` Hann frames of `fftSize`.
Split readBand(const std::vector<float>& y, double rate, size_t fundBin, size_t fftSize, size_t frames,
               double maxHz = 23900.0) {
    const double binHz = rate / static_cast<double>(fftSize);
    const auto maxBin = static_cast<size_t>(maxHz / binHz);
    std::vector<double> nonMag(maxBin, 0.0);
    double harm = 0.0;
    double non = 0.0;
    const size_t start = y.size() - frames * fftSize;
    std::vector<std::complex<double>> a(fftSize);
    for (size_t f = 0; f < frames; ++f) {
        for (size_t i = 0; i < fftSize; ++i) {
            const double w = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) /
                                                  static_cast<double>(fftSize));
            a[i] = {static_cast<double>(y[start + f * fftSize + i]) * w, 0.0};
        }
        fft(a);
        for (size_t b = 3; b < maxBin; ++b) {
            const double p = std::norm(a[b]);
            const size_t r = b % fundBin;
            if (r <= 2 || r >= fundBin - 2) {
                harm += p;
            } else {
                non += p;
                nonMag[b] += p;
            }
        }
    }
    Split s;
    s.nonHarmRelDb = 10.0 * std::log10((non + 1e-30) / (harm + 1e-30));
    // Periodicity: the sampled tone repeats every fftSize samples (whole bins)
    double num = 0.0;
    double den = 0.0;
    for (size_t i = start; i + fftSize < y.size(); ++i) {
        const double d = static_cast<double>(y[i + fftSize]) - static_cast<double>(y[i]);
        num += d * d;
        den += static_cast<double>(y[i]) * static_cast<double>(y[i]);
    }
    s.periodicResidualDb = 10.0 * std::log10((num + 1e-30) / (den + 1e-30));
    std::vector<std::pair<double, size_t>> peaks;
    for (size_t b = 4; b + 1 < maxBin; ++b) {
        if (nonMag[b] > 0.0 && nonMag[b] >= nonMag[b - 1] && nonMag[b] >= nonMag[b + 1]) peaks.emplace_back(nonMag[b], b);
    }
    std::sort(peaks.begin(), peaks.end(), [](const auto& l, const auto& r) { return l.first > r.first; });
    for (size_t k = 0; k < std::min<size_t>(6, peaks.size()); ++k) {
        s.top.emplace_back(static_cast<double>(peaks[k].second) * binHz,
                           10.0 * std::log10(peaks[k].first / (harm + 1e-30)));
    }
    return s;
}

/// Params of node 0 as the Processor's engine holds them for `type` at `driveNorm`.
std::pair<DistortionParams, DistortionCommonParams> engineParams(DistortionType type, double driveNorm,
                                                                 int* factorOut = nullptr) {
    Rig r(kSr);
    r.apply({{kBandCountId, 0.0},
             {node0(NodeParamType::kNodeType), typeNorm(type)},
             {node0(NodeParamType::kNodeMix), 1.0},
             {node0(NodeParamType::kNodeBias), 0.5},
             {node0(NodeParamType::kNodeDrive), driveNorm}});
    r.reactivate();
    const auto in = sine(4096, 1031.25, kSr, 0.5);
    std::vector<float> oL;
    std::vector<float> oR;
    r.render(in, in, oL, oR);
    if (factorOut != nullptr) *factorOut = r.p->getBandProcessor(0).getOversampleFactor();
    const auto* e = r.p->getBandProcessor(0).getMorphEngine();
    return {e->getNodeParams(0), e->getNodeCommonParams(0)};
}

/// The node as an adapter clocked at 48 kHz x n, tone `hz` at -6 dBFS.
std::vector<float> adapterRender(DistortionType type, const DistortionParams& p, const DistortionCommonParams& c,
                                 int n, double hz, size_t frames) {
    const double rate = kSr * n;
    auto a = std::make_unique<DistortionAdapter>();
    a->prepare(rate, 512 * n);
    a->setType(type);
    a->setParams(p);
    a->setCommonParams(c);
    const size_t len = static_cast<size_t>(24000 * n) + frames * kBaseFft * static_cast<size_t>(n);
    const auto x = sine(len, hz, rate, 0.5);
    std::vector<float> y(len);
    for (size_t i = 0; i < len; ++i) y[i] = a->process(x[i]);
    return y;
}

/// Flat-top fraction (|y| within 2 % of its peak) and harmonics 1/3/5 of the last frame.
void tapeShape(const std::vector<float>& y, double rate, double hz, size_t n) {
    const size_t from = y.size() - n;
    double pk = 0.0;
    for (size_t i = from; i < y.size(); ++i) pk = std::max(pk, std::fabs(static_cast<double>(y[i])));
    size_t flat = 0;
    for (size_t i = from; i < y.size(); ++i) flat += std::fabs(static_cast<double>(y[i])) > 0.98 * pk ? 1 : 0;
    std::array<double, 4> h{};
    for (size_t k = 0; k < 4; ++k) {
        std::complex<double> acc(0.0, 0.0);
        const double f = hz * static_cast<double>(2 * k + 1);
        for (size_t i = from; i < y.size(); ++i) {
            acc += static_cast<double>(y[i]) * std::polar(1.0, -2.0 * std::numbers::pi * f * static_cast<double>(i) / rate);
        }
        h[k] = 20.0 * std::log10(2.0 * std::abs(acc) / static_cast<double>(n) + 1e-30);
    }
    std::printf("      peak %.3f, flat-top %.1f %%, H1 %.1f dBFS, H3 %+.1f, H5 %+.1f, H7 %+.1f dB re H1\n", pk,
                100.0 * static_cast<double>(flat) / static_cast<double>(n), h[0], h[1] - h[0], h[2] - h[0], h[3] - h[0]);
}

void printSplit(const char* label, const Split& s) {
    std::printf("%s: non-harmonic %.1f dB, periodicity residual %.1f dB; top:", label, s.nonHarmRelDb,
                s.periodicResidualDb);
    for (const auto& t : s.top) std::printf(" %.0f Hz %.1f;", t.first, t.second);
    std::printf("\n");
}

/// T6.01's processor reading at the band's own factor.
double processorNonHarm(DistortionType type, double driveNorm, int& factor) {
    Rig r(kSr);
    r.apply({{kBandCountId, 0.0},
             {node0(NodeParamType::kNodeType), typeNorm(type)},
             {node0(NodeParamType::kNodeMix), 1.0},
             {node0(NodeParamType::kNodeBias), 0.5},
             {node0(NodeParamType::kNodeDrive), driveNorm}});
    r.reactivate();
    const auto in = sine(24000 + 6 * kBaseFft, 1031.25, kSr, 0.5);
    std::vector<float> oL;
    std::vector<float> oR;
    r.render(in, in, oL, oR);
    factor = r.p->getBandProcessor(0).getOversampleFactor();
    return readAliasing(oL, kSr, 1031.25f, kBaseFft, 4).nonHarmonicRelDb();
}

void splitType(DistortionType type) {
    for (const double d : {0.25, 0.75, 1.0}) {
        int factor = 0;
        const double proc = processorNonHarm(type, d, factor);
        std::printf("== %s drive %.1f: processor (band %dx, IIR oversampler) %.1f dB\n", getTypeName(type), d * 10.0,
                    factor, proc);
        const auto [p, c] = engineParams(type, d);
        char label[160];
        for (const int n : {1, 2, 4, 8, 16}) {
            const auto y = adapterRender(type, p, c, n, 1031.25, 4);
            std::snprintf(label, sizeof label, "   adapter %2dx ideal decimation, 1031.25 Hz", n);
            printSplit(label, readBand(y, kSr * n, 176, kBaseFft * static_cast<size_t>(n), 4));
        }
        for (const auto& [hz, bin] : {std::pair{515.625, size_t{88}}, std::pair{257.8125, size_t{44}}}) {
            const auto y = adapterRender(type, p, c, factor, hz, 4);
            std::snprintf(label, sizeof label, "   adapter %2dx ideal decimation, %.4g Hz", factor, hz);
            printSplit(label, readBand(y, kSr * factor, bin, kBaseFft * static_cast<size_t>(factor), 4));
        }
    }
}

}  // namespace

TEST_CASE("p6r probe: peaks of the normalised types per drive", "[.][p6r-peaks]") {
    for (const auto type : {DistortionType::Fuzz, DistortionType::AsymmetricFuzz, DistortionType::FullRectify,
                            DistortionType::HalfRectify, DistortionType::SoftClip}) {
        for (const double d : {0.1, 0.25, 0.5, 0.75, 1.0}) {
            Rig r(kSr);
            r.apply({{kBandCountId, 0.0},
                     {node0(NodeParamType::kNodeType), typeNorm(type)},
                     {node0(NodeParamType::kNodeMix), 1.0},
                     {node0(NodeParamType::kNodeBias), 0.5},
                     {node0(NodeParamType::kNodeDrive), d}});
            r.reactivate();
            const auto in = sine(24000 + 6 * kBaseFft, 1031.25, kSr, 0.5);
            std::vector<float> oL;
            std::vector<float> oR;
            r.render(in, in, oL, oR);
            const size_t from = 24000;
            std::printf("P6R peak %s drive %.1f: peak %.2f dBFS, RMS %.2f dBFS\n", getTypeName(type), d * 10.0,
                        toDb(peakOf(oL.data() + from, oL.size() - from)),
                        toDb(rmsOf(oL.data() + from, oL.size() - from)));
        }
    }
}

TEST_CASE("p6r probe: non-harmonic split Tape", "[.][p6r-split][p6r-split-tape]") { splitType(DistortionType::Tape); }
TEST_CASE("p6r probe: non-harmonic split Fractal", "[.][p6r-split][p6r-split-fractal]") {
    splitType(DistortionType::Fractal);
}
TEST_CASE("p6r probe: non-harmonic split TriangleFold", "[.][p6r-split][p6r-split-tri]") {
    splitType(DistortionType::TriangleFold);
}
TEST_CASE("p6r probe: non-harmonic split HalfRectify", "[.][p6r-split][p6r-split-half]") {
    splitType(DistortionType::HalfRectify);
}
TEST_CASE("p6r probe: non-harmonic split HardClip control", "[.][p6r-split][p6r-split-hard]") {
    splitType(DistortionType::HardClip);
}

TEST_CASE("p6r probe: bare TapeSaturator", "[.][p6r-tape]") {
    const auto [p, c] = engineParams(DistortionType::Tape, 0.75);
    std::printf("Tape node defaults: model %d, sag %.3f, speed %.3f (drive %.2f dB), bias %.3f, hfRoll %.3f, "
                "flutter %.3f, common drive %.2f\n", p.tapeModel, p.sag, p.speed, 12.0 - p.speed * 10.0, p.bias,
                p.hfRoll, p.flutter, c.drive);
    using Krate::DSP::HysteresisSolver;
    using Krate::DSP::TapeModel;
    struct V {
        const char* name = "";
        TapeModel model = TapeModel::Hysteresis;
        HysteresisSolver solver = HysteresisSolver::RK4;
        bool fixedTScale = false;
        float bias = 0.0f;
        float inDrive = 1.0f;
        float sat = -1.0f;  // -1: the node's Sag
    };
    const float d = c.drive;
    const auto mk = [](const char* name, TapeModel model, HysteresisSolver solver, bool fixedTScale, float bias,
                       float inDrive, float sat = -1.0f) {
        return V{.name = name, .model = model, .solver = solver, .fixedTScale = fixedTScale, .bias = bias,
                 .inDrive = inDrive, .sat = sat};
    };
    const V vs[] = {
        mk("Hysteresis RK4 (as the node)", TapeModel::Hysteresis, HysteresisSolver::RK4, false, p.bias, d),
        mk("Hysteresis RK4, TScale 1 at every rate", TapeModel::Hysteresis, HysteresisSolver::RK4, true, p.bias, d),
        mk("Hysteresis RK2", TapeModel::Hysteresis, HysteresisSolver::RK2, false, p.bias, d),
        mk("Hysteresis NR8", TapeModel::Hysteresis, HysteresisSolver::NR8, false, p.bias, d),
        mk("Hysteresis RK4 bias 0", TapeModel::Hysteresis, HysteresisSolver::RK4, false, 0.0f, d),
        mk("Hysteresis RK4 input drive 1", TapeModel::Hysteresis, HysteresisSolver::RK4, false, p.bias, 1.0f),
        mk("Hysteresis RK4 input drive 0.25", TapeModel::Hysteresis, HysteresisSolver::RK4, false, p.bias, 0.25f),
        mk("Simple", TapeModel::Simple, HysteresisSolver::RK4, false, p.bias, d),
        mk("Hysteresis RK4 saturation 1", TapeModel::Hysteresis, HysteresisSolver::RK4, false, p.bias, d, 1.0f),
        mk("Hysteresis RK4 saturation 0.25", TapeModel::Hysteresis, HysteresisSolver::RK4, false, p.bias, d, 0.25f),
        mk("Hysteresis RK4 input drive 0.01", TapeModel::Hysteresis, HysteresisSolver::RK4, false, p.bias, 0.01f),
        mk("Hysteresis RK4 input drive 0.001", TapeModel::Hysteresis, HysteresisSolver::RK4, false, p.bias, 0.001f),
    };
    for (const auto& v : vs) {
        for (const int n : {1, 2, 4, 8, 16}) {
            for (const auto& [hz, bin] : {std::pair{1031.25, size_t{176}}, std::pair{257.8125, size_t{44}}}) {
                if (hz < 1000.0 && n != 2) continue;
                const double rate = kSr * n;
                Krate::DSP::TapeSaturator t;
                t.prepare(v.fixedTScale ? 44100.0 : rate, 512);
                t.setModel(v.model);
                t.setSolver(v.solver);
                t.setDrive(12.0f - p.speed * 10.0f);
                t.setSaturation(v.sat >= 0.0f ? v.sat : p.sag);
                t.setBias(v.bias);
                t.reset();
                const size_t fftSize = kBaseFft * static_cast<size_t>(n);
                const size_t len = static_cast<size_t>(24000 * n) + 4 * fftSize;
                auto y = sine(len, hz, rate, 0.5 * v.inDrive);
                for (size_t i = 0; i < len; i += 512) t.process(y.data() + i, std::min<size_t>(512, len - i));
                char label[200];
                std::snprintf(label, sizeof label, "  %-40s %2dx %7.2f Hz", v.name, n, hz);
                printSplit(label, readBand(y, rate, bin, fftSize, 4));
                tapeShape(y, rate, hz, fftSize);
            }
        }
    }
}

namespace {

/// Group delay (samples at 48 kHz) of h at f, by the phase slope over f +/- 2 Hz.
double gdAt(const std::vector<float>& h, double f) {
    const auto ph = [&](double fq) {
        double re = 0.0;
        double im = 0.0;
        for (size_t n = 0; n < h.size(); ++n) {
            const double a = -2.0 * std::numbers::pi * fq * static_cast<double>(n) / kSr;
            re += static_cast<double>(h[n]) * std::cos(a);
            im += static_cast<double>(h[n]) * std::sin(a);
        }
        return std::complex<double>(re, im);
    };
    const double dphi = std::arg(ph(f + 2.0) / ph(f - 2.0));
    return -dphi / (2.0 * std::numbers::pi * 4.0) * kSr;
}

std::complex<double> hAt(const std::vector<float>& h, double f) {
    std::complex<double> s(0.0, 0.0);
    for (size_t n = 0; n < h.size(); ++n) {
        s += static_cast<double>(h[n]) * std::polar(1.0, -2.0 * std::numbers::pi * f * static_cast<double>(n) / kSr);
    }
    return s;
}

template <typename Os>
std::vector<float> osImpulse() {
    Os os;
    os.prepare(kSr, 512, Krate::DSP::OversamplingQuality::Economy, Krate::DSP::OversamplingMode::ZeroLatency);
    std::vector<float> l(512, 0.0f);
    std::vector<float> r(512, 0.0f);
    l[0] = 1.0f;
    r[0] = 1.0f;
    os.process(l.data(), r.data(), 512, [](float*, float*, size_t) {});
    return l;
}

constexpr double kFreqs[] = {20.0, 100.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 15000.0, 18000.0, 20000.0};

double osMaxNorm(int factor) {
    if (factor <= 1) return 0.0;
    return factor == 2 ? 1.0 / 3.0 : 2.0 / 3.0;
}

constexpr size_t kAt = 9600;
constexpr size_t kLen = kAt + 4096;

/// Left output from an impulse 0.25 at kAt, from kAt on, of `l` with band `solo` soloed (-1 none).
std::vector<float> impulseFrom(PList l, int solo, long* reported = nullptr) {
    if (solo >= 0) l.emplace_back(makeBandParamId(static_cast<uint8_t>(solo), BandParamType::kBandSolo), 1.0);
    Rig rig(kSr);
    rig.apply(l);
    rig.reactivate();
    std::vector<float> in(kLen, 0.0f);
    in[kAt] = 0.25f;
    std::vector<float> oL;
    std::vector<float> oR;
    rig.render(in, in, oL, oR);
    if (reported != nullptr) *reported = static_cast<long>(rig.p->getLatencySamples());
    std::vector<float> h(oL.begin() + static_cast<std::ptrdiff_t>(kAt), oL.end());
    for (auto& v : h) v *= 4.0f;
    return h;
}

long peakIdx(const std::vector<float>& h) {
    size_t p = 0;
    for (size_t i = 1; i < h.size(); ++i) {
        if (std::fabs(h[i]) > std::fabs(h[p])) p = i;
    }
    return static_cast<long>(p);
}

PList base(int bands, int osMax) {
    PList l{{kBandCountId, static_cast<double>(bands - 1) / 3.0}, {kOversampleMaxId, osMaxNorm(osMax)}};
    for (uint8_t b = 0; b < static_cast<uint8_t>(bands); ++b) {
        l.emplace_back(makeBandParamId(b, BandParamType::kBandMorphX), 0.0);
        l.emplace_back(makeBandParamId(b, BandParamType::kBandMorphY), 0.0);
    }
    return l;
}

void setNode(PList& l, uint8_t band, DistortionType type, double driveNorm, double mixNorm, double toneNorm = -1.0) {
    l.emplace_back(makeNodeParamId(band, 0, NodeParamType::kNodeType), typeNorm(type));
    l.emplace_back(makeNodeParamId(band, 0, NodeParamType::kNodeDrive), driveNorm);
    l.emplace_back(makeNodeParamId(band, 0, NodeParamType::kNodeMix), mixNorm);
    if (toneNorm >= 0.0) l.emplace_back(makeNodeParamId(band, 0, NodeParamType::kNodeTone), toneNorm);
}

}  // namespace

TEST_CASE("p6r probe: oversampler group delay vs impulse peak", "[.][p6r-latency]") {
    const auto h2 = osImpulse<Krate::DSP::Oversampler<2, 2>>();
    const auto h4 = osImpulse<Krate::DSP::Oversampler<4, 2>>();
    for (const auto& [name, h, lats] : {std::tuple{"2x", &h2, std::array<int, 2>{3, 4}},
                                        std::tuple{"4x", &h4, std::array<int, 2>{6, 7}}}) {
        std::printf("Oversampler %s (Economy, ZeroLatency) impulse peak %ld; first samples:", name, peakIdx(*h));
        for (size_t i = 0; i < 12; ++i) std::printf(" %.4f", static_cast<double>((*h)[i]));
        std::printf("\n");
        for (const double f : kFreqs) {
            const auto H = hAt(*h, f);
            std::printf("  %s %6.0f Hz: |H| %+.3f dB, group delay %.3f smp; dry+wet 50%% sum vs dry delayed L=%d: "
                        "%+.3f dB, L=%d: %+.3f dB\n",
                        name, f, 20.0 * std::log10(std::abs(H)), gdAt(*h, f), lats[0],
                        20.0 * std::log10(std::abs(0.5 * std::polar(1.0, -2.0 * std::numbers::pi * f * lats[0] / kSr) +
                                                   0.5 * H)),
                        lats[1],
                        20.0 * std::log10(std::abs(0.5 * std::polar(1.0, -2.0 * std::numbers::pi * f * lats[1] / kSr) +
                                                   0.5 * H)));
        }
    }
    // Processor: 1 band, HardClip drive 10 node mix 0 (only the oversampler's filters)
    for (const int os : {2, 4}) {
        auto l = base(1, os);
        setNode(l, 0, DistortionType::HardClip, 1.0, 0.0);
        long rep = 0;
        const auto h = impulseFrom(l, -1, &rep);
        std::printf("Processor 1 band HardClip node mix 0 at %dx: peak %ld, reported %ld, gd:", os, peakIdx(h), rep);
        for (const double f : kFreqs) std::printf(" %.0f Hz %.3f;", f, gdAt(h, f));
        std::printf("\n");
    }
}

TEST_CASE("p6r probe: band arrival as fractional group delay", "[.][p6r-arrival]") {
    const double fBand[2] = {150.0, 3000.0};
    struct Cfg {
        const char* name;
        DistortionType other;
        double drive;
        double tone;  // -1: default
    };
    const Cfg cfgs[] = {
        {.name = "HardClip + Bitcrush, drive 0.1, Tone 8 kHz (the probe)", .other = DistortionType::Bitcrush, .drive = 0.01, .tone = 1.0},
        {.name = "HardClip + SoftClip, drive 0.1, Tone 8 kHz (the probe)", .other = DistortionType::SoftClip, .drive = 0.01, .tone = 1.0},
        {.name = "HardClip + Bitcrush, drive 10 node mix 0", .other = DistortionType::Bitcrush, .drive = 1.0, .tone = -1.0},
        {.name = "HardClip + SoftClip, drive 10 node mix 0", .other = DistortionType::SoftClip, .drive = 1.0, .tone = -1.0},
    };
    for (const auto& c : cfgs) {
        const bool mix0 = c.drive >= 1.0;
        const auto make = [&](int osMax, double drive) {
            auto l = base(2, osMax);
            setNode(l, 0, DistortionType::HardClip, drive, mix0 ? 0.0 : 1.0, c.tone);
            setNode(l, 1, c.other, drive, mix0 ? 0.0 : 1.0, c.tone);
            return l;
        };
        std::printf("== %s\n", c.name);
        for (int b = 0; b < 2; ++b) {
            long rep = 0;
            const auto hc = impulseFrom(make(4, c.drive), b, &rep);
            const auto hr0 = impulseFrom(make(1, 0.0), b);       // the probe's reference: drive 0 at 1x
            const auto hr1 = impulseFrom(make(1, c.drive), b);   // same node path at 1x
            std::printf("  band %d: peak cfg %ld / ref(drive 0) %ld / ref(same drive, 1x) %ld; reported %ld\n", b,
                        peakIdx(hc), peakIdx(hr0), peakIdx(hr1), rep);
            for (const double f : {fBand[b] * 0.5, fBand[b], fBand[b] * 2.0}) {
                std::printf("    %6.0f Hz: gd cfg %.3f, ref(drive 0) %.3f, ref(1x) %.3f -> delay vs ref(drive 0) "
                            "%.3f, vs ref(1x) %.3f\n",
                            f, gdAt(hc, f), gdAt(hr0, f), gdAt(hr1, f), gdAt(hc, f) - gdAt(hr0, f),
                            gdAt(hc, f) - gdAt(hr1, f));
            }
        }
    }
}

TEST_CASE("p6r probe: crossover sum with a 1x band and an oversampled band", "[.][p6r-xsum]") {
    for (const double fc : {632.0, 2000.0, 5000.0, 10000.0}) {
        for (const auto [type, os] : {std::pair{DistortionType::SoftClip, 2}, std::pair{DistortionType::HardClip, 4}}) {
            const auto make = [&](int osMax) {
                auto l = base(2, osMax);
                l.emplace_back(makeCrossoverParamId(0), std::log10(fc / 20.0) / 3.0);
                setNode(l, 0, DistortionType::SoftClip, 0.0, 1.0);  // passthrough, 1x
                setNode(l, 1, type, 1.0, 0.0);                     // only the oversampler's filters
                return l;
            };
            const auto hc = impulseFrom(make(os), -1);
            const auto hr = impulseFrom(make(1), -1);
            std::printf("crossover %.0f Hz, band 1 at %dx vs all 1x:", fc, os);
            for (const double r : {0.5, 0.71, 1.0, 1.41, 2.0}) {
                const double f = fc * r;
                if (f > 21000.0) continue;
                std::printf(" %.0f Hz %+.3f dB;", f, 20.0 * std::log10(std::abs(hAt(hc, f)) / std::abs(hAt(hr, f))));
            }
            std::printf("\n");
        }
    }
}

TEST_CASE("p6r probe: Tape Sag through the Processor", "[.][p6r-tape-sag]") {
    for (const int osMax : {1, 4}) {
        for (const double sag : {0.5, 0.75, 0.9, 1.0}) {
            for (const double d : {0.25, 0.75, 1.0}) {
                Rig r(kSr);
                r.apply({{kBandCountId, 0.0},
                         {kOversampleMaxId, osMaxNorm(osMax)},
                         {node0(NodeParamType::kNodeType), typeNorm(DistortionType::Tape)},
                         {node0(NodeParamType::kNodeMix), 1.0},
                         {node0(NodeParamType::kNodeBias), 0.5},
                         {node0(NodeParamType::kNodeShape1), sag},
                         {node0(NodeParamType::kNodeDrive), d}});
                r.reactivate();
                const auto in = sine(24000 + 6 * kBaseFft, 1031.25, kSr, 0.5);
                std::vector<float> oL;
                std::vector<float> oR;
                r.render(in, in, oL, oR);
                size_t nonFinite = 0;
                for (const float v : oL) nonFinite += fin(v) ? 0 : 1;
                const auto a = readAliasing(oL, kSr, 1031.25f, kBaseFft, 4);
                std::printf("Tape Oversample Max %dx (band %dx) Sag %.2f drive %.1f: non-finite %zu, peak %.2f dBFS, "
                            "fundamental %.1f dB, non-harmonic %.1f dB\n",
                            osMax, r.p->getBandProcessor(0).getOversampleFactor(), sag, d * 10.0, nonFinite,
                            toDb(peakOf(oL.data() + 24000, oL.size() - 24000)), a.fundamentalDb, a.nonHarmonicRelDb());
            }
        }
    }
    const auto [hp, hc] = engineParams(DistortionType::HalfRectify, 0.25);
    std::printf("HalfRectify node defaults: threshold %.3f, smoothness %.3f, dcBlock %d\n", hp.threshold,
                hp.smoothness, hp.dcBlock ? 1 : 0);
    const auto [fp, fc] = engineParams(DistortionType::Fractal, 0.25);
    std::printf("Fractal node defaults: mode %d, iterations %d, scale %.3f, curve %.3f, blend %d\n", fp.fractalMode,
                fp.iterations, fp.scaleFactor, fp.fractalCurve, fp.fractalBlend);
    const auto [tp, tc] = engineParams(DistortionType::TriangleFold, 0.25);
    std::printf("TriangleFold node defaults: folds %.3f, symmetry %.3f, angle %.3f, smoothness %.3f\n", tp.folds,
                tp.symmetry, tp.angle, tp.smoothness);
}

namespace {

// Bare shapers (no tone / DC filters), the defaults of the fresh nodes: TriangleFold folds 2
// (threshold 0.5, makeup x2), HalfRectify threshold 0.5.
double triFold(double x, double t) {
    const double p = std::fmod(std::fabs(x) + t, 4.0 * t);
    const double tri = p < 2.0 * t ? p - t : 3.0 * t - p;
    return x < 0.0 ? -tri : tri;
}
/// Antiderivative of triFold (even, periodic: the triangle integrates to 0 over a period).
double triFoldF(double x, double t) {
    const double p = std::fmod(std::fabs(x) + t, 4.0 * t);
    const double g = p < 2.0 * t ? 0.5 * p * p - t * p : 3.0 * t * (p - 2.0 * t) - 0.5 * (p * p - 4.0 * t * t);
    const double g0 = 0.5 * t * t - t * t;  // G(t): F(0) = 0
    return g - g0;
}
double halfRect(double x, double t) { return x >= t ? x : 0.0; }
double halfRectF(double x, double t) { return x >= t ? 0.5 * (x * x - t * t) : 0.0; }

template <typename F, typename AF>
std::vector<float> shape(const std::vector<float>& x, bool adaa, F f, AF af) {
    std::vector<float> y(x.size());
    double prev = 0.0;
    for (size_t i = 0; i < x.size(); ++i) {
        const double xi = x[i];
        const double d = xi - prev;
        double v = f(xi);
        if (adaa) v = std::fabs(d) > 1e-6 ? (af(xi) - af(prev)) / d : f(0.5 * (xi + prev));
        y[i] = static_cast<float>(v);
        prev = xi;
    }
    return y;
}

}  // namespace

TEST_CASE("p6r probe: ADAA1 on the bare fold and rectifier", "[.][p6r-adaa]") {
    for (const double drive : {2.5, 7.5, 10.0}) {
        for (const int n : {4, 8}) {
            const double rate = kSr * n;
            const size_t fftSize = kBaseFft * static_cast<size_t>(n);
            const auto x = sine(static_cast<size_t>(4800 * n) + 4 * fftSize, 1031.25, rate, 0.5 * drive);
            const auto tri = [](double v) { return 2.0 * triFold(v, 0.5); };
            const auto triF = [](double v) { return 2.0 * triFoldF(v, 0.5); };
            const auto half = [](double v) { return halfRect(v, 0.5); };
            const auto halfF = [](double v) { return halfRectF(v, 0.5); };
            const auto halfCont = [](double v) { return v >= 0.5 ? v - 0.5 : 0.0; };
            const auto halfContF = [](double v) { return v >= 0.5 ? 0.5 * (v - 0.5) * (v - 0.5) : 0.0; };
            std::printf("drive %.1f at %dx (ideal decimation): TriangleFold plain %.1f, ADAA1 %.1f dB; HalfRectify "
                        "plain %.1f, ADAA1 %.1f dB; HalfRectify continuous (x - t) plain %.1f, ADAA1 %.1f dB\n",
                        drive, n, readBand(shape(x, false, tri, triF), rate, 176, fftSize, 4).nonHarmRelDb,
                        readBand(shape(x, true, tri, triF), rate, 176, fftSize, 4).nonHarmRelDb,
                        readBand(shape(x, false, half, halfF), rate, 176, fftSize, 4).nonHarmRelDb,
                        readBand(shape(x, true, half, halfF), rate, 176, fftSize, 4).nonHarmRelDb,
                        readBand(shape(x, false, halfCont, halfContF), rate, 176, fftSize, 4).nonHarmRelDb,
                        readBand(shape(x, true, halfCont, halfContF), rate, 176, fftSize, 4).nonHarmRelDb);
        }
    }
}

// =============================================================================
// [p6s-ab-dump] / [p6s-ab-compare]: the ADAA ruling (2026-11-09) A/B. HalfRectify and
// TriangleFold through the Processor (one band, node 0, mix 1, bias 0) at drive 2.5 / 7.5 /
// 10 on three signals: a 99.6 Hz sine at -6 dBFS (H1..H9; bin 17 of 8192, so no alias of the
// 4x clocked rate lands on a harmonic), the T6.01 tone (non-harmonic)
// and a music-like mix (four decaying notes and a little noise). Run the dump case with the
// binary before the change and the compare case with the one after, both from the same
// working directory (the renders are p6s_ab_*.bin there).
// =============================================================================

namespace {

constexpr size_t kAbWarm = 24000;
constexpr size_t kAbLen = kAbWarm + 6 * kBaseFft;
constexpr double kAbF0 = 17.0 * kSr / static_cast<double>(kBaseFft);  // 99.609375 Hz

std::vector<float> abSignal(int which) {
    std::vector<float> x(kAbLen, 0.0f);
    if (which == 0) return sine(kAbLen, kAbF0, kSr, 0.5);
    if (which == 1) return sine(kAbLen, 1031.25, kSr, 0.5);
    const double notes[] = {110.0, 164.81, 277.18, 440.0};
    uint32_t s = 0x2468u;
    for (size_t i = 0; i < kAbLen; ++i) {
        const double t = static_cast<double>(i) / kSr;
        double v = 0.0;
        for (size_t k = 0; k < 4; ++k) {
            const double since = std::fmod(t + 0.11 * static_cast<double>(k), 0.5);
            v += 0.12 * std::exp(-since / 0.3) * std::sin(2.0 * std::numbers::pi * notes[k] * t);
        }
        s = s * 1664525u + 1013904223u;
        v += 0.01 * (static_cast<double>(s >> 8) / 8388608.0 - 1.0);
        x[i] = static_cast<float>(v);
    }
    return x;
}

const char* const kAbSignalNames[] = {"sine99", "tone1k", "music"};

std::vector<float> abRender(DistortionType type, double driveNorm, int which) {
    Rig r(kSr);
    r.apply({{kBandCountId, 0.0},
             {node0(NodeParamType::kNodeType), typeNorm(type)},
             {node0(NodeParamType::kNodeMix), 1.0},
             {node0(NodeParamType::kNodeBias), 0.5},
             {node0(NodeParamType::kNodeDrive), driveNorm}});
    r.reactivate();
    const auto in = abSignal(which);
    std::vector<float> oL;
    std::vector<float> oR;
    r.render(in, in, oL, oR);
    return oL;
}

std::string abPath(DistortionType type, double driveNorm, int which) {
    return std::string("p6s_ab_") + getTypeName(type) + "_" + std::to_string(static_cast<int>(driveNorm * 100.0)) +
           "_" + kAbSignalNames[which] + ".bin";
}

/// |X| in dB of y[from, from + n) at hz (n whole cycles).
double abToneDb(const std::vector<float>& y, size_t from, size_t n, double hz) {
    std::complex<double> acc(0.0, 0.0);
    for (size_t i = 0; i < n; ++i) {
        acc += static_cast<double>(y[from + i]) *
               std::polar(1.0, -2.0 * std::numbers::pi * hz * static_cast<double>(i) / kSr);
    }
    return 20.0 * std::log10(2.0 * std::abs(acc) / static_cast<double>(n) + 1e-30);
}

constexpr DistortionType kAbTypes[] = {DistortionType::HalfRectify, DistortionType::TriangleFold};
constexpr double kAbDrives[] = {0.25, 0.75, 1.0};

}  // namespace

TEST_CASE("p6s probe: ADAA A/B, dump the renders", "[.][p6s-ab-dump]") {
    for (const auto type : kAbTypes) {
        for (const double d : kAbDrives) {
            for (int which = 0; which < 3; ++which) {
                const auto y = abRender(type, d, which);
                FILE* f = std::fopen(abPath(type, d, which).c_str(), "wb");
                REQUIRE(f != nullptr);
                std::fwrite(y.data(), sizeof(float), y.size(), f);
                std::fclose(f);
            }
        }
    }
}

TEST_CASE("p6s probe: ADAA A/B, compare with the dumped renders", "[.][p6s-ab-compare]") {
    for (const auto type : kAbTypes) {
        for (const double d : kAbDrives) {
            for (int which = 0; which < 3; ++which) {
                const auto after = abRender(type, d, which);
                std::vector<float> before(after.size(), 0.0f);
                FILE* f = std::fopen(abPath(type, d, which).c_str(), "rb");
                REQUIRE(f != nullptr);
                const size_t got = std::fread(before.data(), sizeof(float), before.size(), f);
                std::fclose(f);
                REQUIRE(got == before.size());
                const size_t n = after.size() - kAbWarm;
                const double levelDb = toDb(rmsOf(after.data() + kAbWarm, n)) - toDb(rmsOf(before.data() + kAbWarm, n));
                std::printf("P6S A/B %s drive %.1f %s: level %+.3f dB", getTypeName(type), d * 10.0,
                            kAbSignalNames[which], levelDb);
                if (which == 0) {
                    // 102 whole cycles; harmonics more than 80 dB below H1 are not compared
                    const double h1 = abToneDb(before, kAbWarm, n, kAbF0);
                    double worst = 0.0;
                    std::printf("; H1..H9 before/after (dB):");
                    for (int k = 1; k <= 9; ++k) {
                        const double b = abToneDb(before, kAbWarm, n, kAbF0 * k);
                        const double a = abToneDb(after, kAbWarm, n, kAbF0 * k);
                        std::printf(" H%d %.2f/%.2f", k, b, a);
                        if (b > h1 - 80.0) worst = std::max(worst, std::fabs(a - b));
                    }
                    std::printf("; largest |diff| %.4f dB", worst);
                    INFO(getTypeName(type) << " drive " << d * 10.0 << ": largest harmonic diff " << worst << " dB");
                    CHECK(worst <= 0.1);
                } else if (which == 1) {
                    std::printf("; non-harmonic %.1f -> %.1f dB",
                                readAliasing(before, kSr, 1031.25f, kBaseFft, 4).nonHarmonicRelDb(),
                                readAliasing(after, kSr, 1031.25f, kBaseFft, 4).nonHarmonicRelDb());
                }
                std::printf("\n");
            }
        }
    }
}

// =============================================================================
// [p6s-flatness]: the follow-up 81 flatness bars per frequency. One band at 2x / 4x (node mix
// 0: the oversampler only) at Mix 100 % (its own |H|) and Mix 50 %, and a 1x + 2x / 4x band
// pair split at 5 kHz, each against the same at Oversample Max 1x, at 44.1 / 48 / 96 kHz.
// =============================================================================

namespace {

std::vector<float> flatResponse(const PList& l, double sr) {
    Rig rig(sr);
    rig.apply(l);
    rig.reactivate();
    std::vector<float> in(kAt + 8192, 0.0f);
    in[kAt] = 0.25f;
    std::vector<float> oL;
    std::vector<float> oR;
    rig.render(in, in, oL, oR);
    std::vector<float> h(oL.begin() + static_cast<std::ptrdiff_t>(kAt), oL.end());
    for (auto& v : h) v *= 4.0f;
    return h;
}

}  // namespace

TEST_CASE("p6s probe: flatness of Mix 50 % and the crossover sum per frequency", "[.][p6s-flatness]") {
    const double freqs[] = {1000.0, 2000.0, 5000.0, 8000.0, 10000.0, 12000.0, 14000.0, 16000.0, 18000.0, 20000.0};
    for (const double sr : {44100.0, 48000.0, 96000.0, 192000.0}) {
        for (const int os : {2, 4}) {
            const DistortionType t = os == 4 ? DistortionType::HardClip : DistortionType::SoftClip;
            for (const double mix : {1.0, 0.5}) {
                const auto make = [&](int osMax) {
                    auto l = base(1, osMax);
                    setNode(l, 0, t, 1.0, 0.0);
                    l.emplace_back(kGlobalMixId, mix);
                    return l;
                };
                const auto h = flatResponse(make(os), sr);
                const auto r = flatResponse(make(1), sr);
                std::printf("P6S flat %.0f Hz, band at %dx, Mix %.0f %% (arrival at 1 kHz %.3f):", sr, os, mix * 100.0,
                            groupDelaySamples(h, 1000.0, sr) - groupDelaySamples(r, 1000.0, sr));
                for (const double f : freqs) std::printf(" %.0fk %+.3f", f / 1000.0, responseDb(h, f, sr) - responseDb(r, f, sr));
                std::printf("\n");
            }
            const auto pair = [&](int osMax) {
                auto l = base(2, osMax);
                l.emplace_back(makeCrossoverParamId(0), std::log10(5000.0 / 20.0) / 3.0);
                setNode(l, 0, DistortionType::Bitcrush, 1.0, 0.0);
                setNode(l, 1, t, 1.0, 0.0);
                return l;
            };
            const auto h = flatResponse(pair(os), sr);
            const auto r = flatResponse(pair(1), sr);
            std::printf("P6S flat %.0f Hz, 1x + %dx bands at 5 kHz:", sr, os);
            for (const double f : freqs) std::printf(" %.0fk %+.3f", f / 1000.0, responseDb(h, f, sr) - responseDb(r, f, sr));
            std::printf("\n");
        }
    }
}

// =============================================================================
// [p6s-minimax]: the minimax alignment ruling (2026-11-09). Model: the dry path and a 1x
// band arrive at L exactly; a 2x / 4x band is its oversampler H_f (measured impulse
// response) delayed to arrive at L + d_f (d_f = 0: the low-frequency group delay match,
// ffd51f47; d_f = gd_f - impulse peak: the old integer alignment). Relative to the dry path a
// band is T_f = H_f e^{j w (gd_f - d_f)}. Over 20 Hz - 20 kHz (1/24 octave):
// - Mix 50 %: 0.5 + 0.5 T_f (raw, against flat; "excess" divides out 0.5 + 0.5 |T_f|, the
//   part no delay changes);
// - crossover sum of bands a / b: an LR4 split weights them by in-phase alpha / 1 - alpha;
//   raw = alpha T_a + (1 - alpha) T_b over crossovers 50 Hz - 16 kHz (1/3 octave);
//   "cancel" = its worst over every alpha re alpha |T_a| + (1 - alpha) |T_b|: 20 log cos(phi/2).
// Objective per pair: 0.5 max|Mix excess| + 0.5 max|cancel| (2x + 4x: max|cancel|), and the
// minimax d_2, d_4 (0.01-sample grid, d_f >= gd_f - impulse peak so the wait stays >= 0)
// minimise the largest of the three pairs. Raw figures include the oversamplers' own
// passband roll-off, which no delay changes.
// =============================================================================

namespace {

struct MmPath {
    double gd = 0.0;   ///< first-moment group delay (samples)
    int peak = 0;      ///< impulse-peak index (the reported latency's part)
    std::vector<std::complex<double>> h;  ///< H at the frequency grid
};

template <typename Os>
MmPath mmPath(double sr, const std::vector<double>& freqs) {
    Os os;
    os.prepare(sr, 512, Krate::DSP::OversamplingQuality::Economy, Krate::DSP::OversamplingMode::ZeroLatency);
    std::vector<float> ir;
    for (int blk = 0; blk < 4; ++blk) {
        std::vector<float> l(512, 0.0f);
        std::vector<float> r(512, 0.0f);
        if (blk == 0) {
            l[0] = 1.0f;
            r[0] = 1.0f;
        }
        os.process(l.data(), r.data(), 512, [](float*, float*, size_t) {});
        ir.insert(ir.end(), l.begin(), l.end());
    }
    MmPath p;
    double m = 0.0;
    double s = 0.0;
    for (size_t n = 0; n < ir.size(); ++n) {
        m += static_cast<double>(n) * ir[n];
        s += ir[n];
        if (std::fabs(ir[n]) > std::fabs(ir[static_cast<size_t>(p.peak)])) p.peak = static_cast<int>(n);
    }
    p.gd = m / s;
    for (const double f : freqs) {
        std::complex<double> acc(0.0, 0.0);
        for (size_t n = 0; n < ir.size(); ++n) {
            acc += static_cast<double>(ir[n]) * std::polar(1.0, -2.0 * std::numbers::pi * f * static_cast<double>(n) / sr);
        }
        p.h.push_back(acc);
    }
    return p;
}

struct MmWorst {
    double db = 0.0;
    double hz = 0.0;
    double fc = 0.0;
    void take(double v, double f, double c = 0.0) {
        if (std::fabs(v) > std::fabs(db)) {
            db = v;
            hz = f;
            fc = c;
        }
    }
};

struct MmPair {
    MmWorst mixRaw, mixExcess, xRaw, xCancel;
};

/// Metrics of bands a / b (1 = the dry path or a 1x band) with T_a, T_b.
MmPair mmMetrics(const std::vector<double>& freqs, double sr, const std::vector<std::complex<double>>& ta,
                 const std::vector<std::complex<double>>& tb, bool withMix, bool withRaw) {
    MmPair r;
    for (size_t i = 0; i < freqs.size(); ++i) {
        const double f = freqs[i];
        const auto a = ta[i];
        const auto b = tb[i];
        const double phi = std::arg(b / a);
        r.xCancel.take(20.0 * std::log10(std::max(std::fabs(std::cos(0.5 * phi)), 1e-10)), f);
        if (withMix) {
            const double raw = 20.0 * std::log10(std::abs(0.5 * a + 0.5 * b));
            r.mixRaw.take(raw, f);
            r.mixExcess.take(raw - 20.0 * std::log10(0.5 * std::abs(a) + 0.5 * std::abs(b)), f);
        }
        if (withRaw) {
            for (double fc = 50.0; fc <= 16000.0; fc *= std::pow(2.0, 1.0 / 3.0)) {
                const double q = std::pow(f / fc, 4.0);
                const double alpha = 1.0 / (1.0 + q);
                r.xRaw.take(20.0 * std::log10(std::abs(alpha * a + (1.0 - alpha) * b)), f, fc);
            }
        }
    }
    (void)sr;
    return r;
}

std::vector<std::complex<double>> mmShift(const MmPath& p, const std::vector<double>& freqs, double sr, double d) {
    std::vector<std::complex<double>> t(freqs.size());
    for (size_t i = 0; i < freqs.size(); ++i) {
        t[i] = p.h[i] * std::polar(1.0, 2.0 * std::numbers::pi * freqs[i] / sr * (p.gd - d));
    }
    return t;
}

double mmCost(const MmPair& m, bool withMix) {
    return withMix ? 0.5 * std::fabs(m.mixExcess.db) + 0.5 * std::fabs(m.xCancel.db) : std::fabs(m.xCancel.db);
}

}  // namespace

TEST_CASE("p6s probe: minimax alignment delays per factor and rate", "[.][p6s-minimax]") {
    std::vector<double> freqs;
    for (double f = 20.0; f <= 20000.0 * 1.0001; f *= std::pow(2.0, 1.0 / 24.0)) freqs.push_back(f);
    for (const double sr : {44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0}) {
        const auto p2 = mmPath<Krate::DSP::Oversampler<2, 2>>(sr, freqs);
        const auto p4 = mmPath<Krate::DSP::Oversampler<4, 2>>(sr, freqs);
        const std::vector<std::complex<double>> one(freqs.size(), {1.0, 0.0});
        const double lo2 = p2.gd - p2.peak;
        const double lo4 = p4.gd - p4.peak;
        // 1-D costs of the pairs with the dry path, and of 2x + 4x by d4 - d2
        constexpr double kStep = 0.01;
        const auto grid = [&](double lo) {
            std::vector<double> g;
            for (double d = lo; d <= 2.0 + 1e-9; d += kStep) g.push_back(d);
            return g;
        };
        const auto g2 = grid(lo2);
        const auto g4 = grid(lo4);
        std::vector<double> c12;
        std::vector<double> c14;
        c12.reserve(g2.size());
        c14.reserve(g4.size());
        for (const double d : g2) c12.push_back(mmCost(mmMetrics(freqs, sr, one, mmShift(p2, freqs, sr, d), true, false), true));
        for (const double d : g4) c14.push_back(mmCost(mmMetrics(freqs, sr, one, mmShift(p4, freqs, sr, d), true, false), true));
        const auto t2zero = mmShift(p2, freqs, sr, 0.0);
        // Full 0.01-sample grid: the smallest max of the three pair costs, then among the grid
        // points within 0.001 dB of it the smallest sum (the max alone leaves d2 free where the
        // 1x + 4x pair pins it)
        std::vector<std::vector<std::complex<double>>> t2s;
        std::vector<std::vector<std::complex<double>>> t4s;
        t2s.reserve(g2.size());
        t4s.reserve(g4.size());
        for (const double d : g2) t2s.push_back(mmShift(p2, freqs, sr, d));
        for (const double d : g4) t4s.push_back(mmShift(p4, freqs, sr, d));
        std::vector<double> cAll(g2.size() * g4.size());
        std::vector<double> sAll(g2.size() * g4.size());
        double best = 1e300;
        for (size_t i = 0; i < g2.size(); ++i) {
            for (size_t k = 0; k < g4.size(); ++k) {
                const double c24 = mmCost(mmMetrics(freqs, sr, t2s[i], t4s[k], false, false), false);
                cAll[i * g4.size() + k] = std::max({c12[i], c14[k], c24});
                sAll[i * g4.size() + k] = c12[i] + c14[k] + c24;
                best = std::min(best, cAll[i * g4.size() + k]);
            }
        }
        double bestSum = 1e300;
        double bd2 = 0.0;
        double bd4 = 0.0;
        for (size_t i = 0; i < g2.size(); ++i) {
            for (size_t k = 0; k < g4.size(); ++k) {
                const size_t j = i * g4.size() + k;
                if (cAll[j] <= best + 1e-3 && sAll[j] < bestSum) {
                    bestSum = sAll[j];
                    bd2 = g2[i];
                    bd4 = g4[k];
                }
            }
        }
        (void)t2zero;
        const auto i12 = static_cast<size_t>(std::min_element(c12.begin(), c12.end()) - c12.begin());
        const auto i14 = static_cast<size_t>(std::min_element(c14.begin(), c14.end()) - c14.begin());
        std::printf("MM %.0f Hz: pair-alone optima: 1x+2x d2 %+.3f (cost %.3f), 1x+4x d4 %+.3f (cost %.3f)\n", sr, g2[i12],
                    c12[i12], g4[i14], c14[i14]);
        std::printf("MM %.0f Hz: oversampler gd 2x %.4f (peak %d), 4x %.4f (peak %d); minimax d2 %+.3f d4 %+.3f "
                    "(cost %.3f dB)\n", sr, p2.gd, p2.peak, p4.gd, p4.peak, bd2, bd4, best);
        struct Align {
            const char* name;
            double d2;
            double d4;
        };
        for (const Align& al : {Align{.name = "old-integer", .d2 = lo2, .d4 = lo4}, Align{.name = "lf-match", .d2 = 0.0, .d4 = 0.0},
                                Align{.name = "minimax", .d2 = bd2, .d4 = bd4}}) {
            const auto t2 = mmShift(p2, freqs, sr, al.d2);
            const auto t4 = mmShift(p4, freqs, sr, al.d4);
            const auto m12 = mmMetrics(freqs, sr, one, t2, true, true);
            const auto m14 = mmMetrics(freqs, sr, one, t4, true, true);
            const auto m24 = mmMetrics(freqs, sr, t2, t4, false, true);
            std::printf("MM %.0f %-11s d2 %+.3f d4 %+.3f | Mix50 2x %+.2f@%.0f (ex %+.2f) 4x %+.2f@%.0f (ex %+.2f) | "
                        "xsum raw 1+2 %+.2f@%.0f/%.0f 1+4 %+.2f@%.0f/%.0f 2+4 %+.2f@%.0f/%.0f | cancel 1+2 %+.2f@%.0f "
                        "1+4 %+.2f@%.0f 2+4 %+.2f@%.0f\n",
                        sr, al.name, al.d2, al.d4, m12.mixRaw.db, m12.mixRaw.hz, m12.mixExcess.db, m14.mixRaw.db,
                        m14.mixRaw.hz, m14.mixExcess.db, m12.xRaw.db, m12.xRaw.hz, m12.xRaw.fc, m14.xRaw.db, m14.xRaw.hz,
                        m14.xRaw.fc, m24.xRaw.db, m24.xRaw.hz, m24.xRaw.fc, m12.xCancel.db, m12.xCancel.hz,
                        m14.xCancel.db, m14.xCancel.hz, m24.xCancel.db, m24.xCancel.hz);
        }
    }
}

// =============================================================================
// [p6d-ringsat] Ring Saturation A/B for the 2026-11-10 ruling (follow-up 84): the carrier
// phase and the zero-crossing tracker advance once per sample (they advanced once per
// stage), and a factor crossfade carries them into the incoming lane. One band, the node
// alone at drive 3, 222.65625 Hz (bin 38 of 8192 at 48 kHz) at -12 dBFS, 1.5 s; read over
// the last 4 x 8192 samples: RMS, harmonic and non-harmonic power, spectral centroid,
// clicks. Then Oversample Max 4x -> 2x -> 4x at 0.5 / 0.75 s: RMS and clicks of the last
// 0.5 s against the 0.25 s before the first switch.
// =============================================================================

namespace {

struct RingSatConfig {
    const char* name;
    double carrier;   // slot 3: Sine 0, Triangle 1/3, Square 2/3 (default 0.5), Saw 1
    double freqMode;  // slot 5: Fixed 0, Harmonic 1/3, Track 2/3 (default 0.5), Random 1
    double stages;    // slot 1: (stages - 1) / 3 (default 0.5 = 3 stages)
};

double spectralCentroidHz(const std::vector<float>& x, size_t from, double sr) {
    constexpr size_t kN = 8192;
    Krate::DSP::FFT fft;
    fft.prepare(kN);
    std::vector<float> window(kN);
    Krate::DSP::Window::generateHann(window.data(), kN);
    std::vector<float> frame(kN);
    std::vector<Krate::DSP::Complex> spectrum(fft.numBins());
    double num = 0.0;
    double den = 0.0;
    for (size_t start = from; start + kN <= x.size(); start += kN) {
        for (size_t i = 0; i < kN; ++i) frame[i] = x[start + i] * window[i];
        fft.forward(frame.data(), spectrum.data());
        for (size_t k = 1; k < spectrum.size(); ++k) {
            const double p = static_cast<double>(spectrum[k].real) * spectrum[k].real +
                             static_cast<double>(spectrum[k].imag) * spectrum[k].imag;
            num += p * static_cast<double>(k) * sr / static_cast<double>(kN);
            den += p;
        }
    }
    return den > 0.0 ? num / den : 0.0;
}

}  // namespace

TEST_CASE("p6d probe: Ring Saturation A/B", "[.][p6d-ringsat]") {
    constexpr double kTone = 222.65625;
    const auto in = sine(static_cast<size_t>(1.5 * kSr), kTone, kSr, 0.25118864315095801);
    const RingSatConfig configs[] = {
        {.name = "default (Square, Track, 3 stages)", .carrier = 0.5, .freqMode = 0.5, .stages = 0.5},
        {.name = "Square, Track, 1 stage", .carrier = 0.5, .freqMode = 0.5, .stages = 0.0},
        {.name = "Sine, Track, 3 stages", .carrier = 0.0, .freqMode = 0.5, .stages = 0.5},
        {.name = "Sine, Harmonic, 3 stages", .carrier = 0.0, .freqMode = 1.0 / 3.0, .stages = 0.5},
        {.name = "Square, Fixed, 3 stages", .carrier = 0.5, .freqMode = 0.0, .stages = 0.5},
        {.name = "Sine, Fixed, 1 stage", .carrier = 0.0, .freqMode = 0.0, .stages = 0.0},
        {.name = "Saw, Random, 2 stages", .carrier = 1.0, .freqMode = 1.0, .stages = 1.0 / 3.0},
    };
    const auto node = [](NodeParamType t) { return makeNodeParamId(0, 0, t); };
    for (const auto& c : configs) {
        for (const double os : {0.0, 1.0 / 3.0, 2.0 / 3.0}) {
            Rig r(kSr);
            r.apply({{kBandCountId, 0.0},
                     {makeBandParamId(0, BandParamType::kBandActiveNodes), 0.0},
                     {kOversampleMaxId, os},
                     {node(NodeParamType::kNodeType), static_cast<double>(static_cast<int>(DistortionType::RingSaturation)) / 25.0},
                     {node(NodeParamType::kNodeDrive), 0.3},
                     {node(NodeParamType::kNodeShape1), c.stages},
                     {node(NodeParamType::kNodeShape3), c.carrier},
                     {node(NodeParamType::kNodeShape5), c.freqMode}});
            r.reactivate();
            std::vector<float> oL;
            std::vector<float> oR;
            r.render(in, in, oL, oR);
            const size_t from = oL.size() - size_t{4} * 8192;
            const std::vector<float> tail(oL.begin() + static_cast<std::ptrdiff_t>(from), oL.end());
            const auto a = readAliasing(tail, kSr, static_cast<float>(kTone), 8192, 4);
            std::printf("RINGSAT %-34s %dx: rms %.2f dB, harmonic %.2f dB, non-harmonic %+.2f dB re harmonic, "
                        "centroid %.0f Hz, clicks %zu\n",
                        c.name, static_cast<int>(std::lround(std::pow(2.0, std::round(os * 3.0)))),
                        toDb(rmsOf(tail.data(), tail.size())), a.harmonicDb, a.nonHarmonicRelDb(),
                        spectralCentroidHz(oL, from, kSr), clickCount(tail.data(), tail.size(), kSr));
        }
        // Oversample Max 4x -> 2x -> 4x
        Rig r(kSr);
        r.apply({{kBandCountId, 0.0},
                 {makeBandParamId(0, BandParamType::kBandActiveNodes), 0.0},
                 {node(NodeParamType::kNodeType), static_cast<double>(static_cast<int>(DistortionType::RingSaturation)) / 25.0},
                 {node(NodeParamType::kNodeDrive), 0.3},
                 {node(NodeParamType::kNodeShape1), c.stages},
                 {node(NodeParamType::kNodeShape3), c.carrier},
                 {node(NodeParamType::kNodeShape5), c.freqMode}});
        r.reactivate();
        std::vector<float> oL;
        std::vector<float> oR;
        r.render(in, in, oL, oR, 512, [&](size_t, size_t start, PChanges& pc) {
            const double t = static_cast<double>(start) / kSr;
            pc.addChange(kOversampleMaxId, (t >= 0.5 && t < 0.75) ? 1.0 / 3.0 : 2.0 / 3.0);
        });
        const auto S = [](double s) { return static_cast<size_t>(s * kSr); };
        std::printf("RINGSAT %-34s 4x->2x->4x: before %.2f dB (clicks %zu), after return %.2f dB (clicks %zu)\n", c.name,
                    toDb(rmsOf(oL.data() + S(0.25), S(0.25))), clickCount(oL.data() + S(0.25), S(0.25), kSr),
                    toDb(rmsOf(oL.data() + S(1.0), S(0.5))), clickCount(oL.data() + S(1.0), S(0.5), kSr));
    }
}
