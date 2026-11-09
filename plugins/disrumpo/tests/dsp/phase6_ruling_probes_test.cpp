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
