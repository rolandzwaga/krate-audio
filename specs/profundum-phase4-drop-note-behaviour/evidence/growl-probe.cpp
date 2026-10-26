// Phase 4 SC-015 red (T026, 2026-10-13): after a 48-st Drop (T = 1 ms) at kGrowl the steady-state render reads
// "aliased" -30 dBFS at 44.1 kHz but -121 dBFS at 48 kHz (E1). Core alone, no Phase 4 component: render the same
// trajectory through ProfundumCore via f0PerSample and look at WHAT the residue is.
//   growl_probe <fs> <block> <mode>   mode: drop | hold | jump
// Prints the aliased power (BH7, ±8-bin harmonic exclusion, as profundum_core_test_helpers.h) over frames starting
// at trajEnd + 64 and at trajEnd + 64 + L, and the top non-excluded bins (Hz, dB) of the first frame.
#include <krate/dsp/systems/profundum_core.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>
#include <xmmintrin.h>
#include <pmmintrin.h>

using namespace Krate::DSP;
constexpr double kTau2 = 6.283185307179586;
constexpr std::array<double, 7> kBh7{0.27105140069342, 0.43329793923448, 0.21812299954311, 0.06592544638803,
                                     0.01081174209837, 0.00077658482522, 0.00001388721735};
static void fft(std::vector<std::complex<double>>& a) {
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double ang = -kTau2 / double(len);
        for (std::size_t i = 0; i < n; i += len)
            for (std::size_t k = 0; k < len / 2; ++k) {
                const std::complex<double> w(std::cos(ang * double(k)), std::sin(ang * double(k)));
                const std::complex<double> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v; a[i + k + len / 2] = u - v;
            }
    }
}
static std::vector<double> powerSpectrum(const float* x, std::size_t L) {
    std::vector<std::complex<double>> a(L);
    double sxw2 = 0, sw2 = 0;
    for (std::size_t k = 0; k < L; ++k) {
        double w = 0; for (std::size_t j = 0; j < 7; ++j) w += ((j % 2) ? -1.0 : 1.0) * kBh7[j] * std::cos(kTau2 * double(j) * double(k) / double(L));
        const double v = double(x[k]) * w; a[k] = {v, 0}; sxw2 += v * v; sw2 += w * w;
    }
    fft(a);
    std::vector<double> p(L / 2 + 1); double sp = 0;
    for (std::size_t k = 0; k <= L / 2; ++k) { p[k] = std::norm(a[k]); sp += p[k]; }
    const double scale = sp > 0 ? (sxw2 / sw2) / sp : 0; for (double& v : p) v *= scale;
    return p;
}
static double db10(double x) { return 10.0 * std::log10(std::max(x, 1e-30)); }

int main(int argc, char** argv) {
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON); _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
    const double fs = argc > 1 ? std::atof(argv[1]) : 44100.0;
    const std::size_t block = argc > 2 ? std::size_t(std::atoi(argv[2])) : 1;
    const std::string mode = argc > 3 ? argv[3] : "drop";
    const float f0 = argc > 4 ? float(440.0 * std::exp2((std::atof(argv[4]) - 69.0) / 12.0)) : 55.0f;
    const std::size_t L = 65536;
    const std::size_t settle = std::size_t(fs);           // 1 s at the note before the event
    const double dropMs = argc > 5 ? std::atof(argv[5]) : 1.0;
    const std::size_t dropN = std::size_t(std::llround(dropMs * 0.001 * fs));
    const std::size_t total = settle + dropN + 64 + 2 * L + 1024;
    std::vector<float> traj(total, f0);
    if (mode == "drop") {
        for (std::size_t n = 0; n < dropN; ++n) { const double u = double(n) / double(dropN); const double d = (argc > 6 ? std::atof(argv[6]) : 48.0) * std::exp(-8.0 * u) ; traj[settle + n] = float(f0 * std::exp2(d / 12.0)); }
    } else if (mode == "jump") {
        for (std::size_t n = 0; n < dropN; ++n) traj[settle + n] = f0 * 16.0f;   // +48 st held for 1 ms, then back
    }
    std::vector<float> L_(total), R_(total);
    auto core = std::make_unique<ProfundumCore>(); core->prepare(fs);
    core->setControls((argc > 7 && std::string(argv[7]) == "saw") ? SpectralShapeRecipe::kSawAnchor : (argc > 7 && std::string(argv[7]) == "mid") ? SpectralShapeRecipe::Controls{} : SpectralShapeRecipe::kGrowl);
    core->setRetriggerPhase(ProfundumCore::RetriggerPhase::Reset); core->noteOn(f0);
    for (std::size_t p = 0; p < total; p += block) { const std::size_t n = std::min(block, total - p); core->processBlock(L_.data() + p, R_.data() + p, n, traj.data() + p); }
    const std::size_t trajEnd = settle + dropN;
    { double a = 0; for (std::size_t k = settle - 4096; k < settle; ++k) a += double(L_[k]) * L_[k]; std::printf("  rms dBFS before event: %.2f\n", 10 * std::log10(a / 4096 + 1e-30)); }
    std::printf("fs %.0f block %zu mode %s: kGrowl f0 %.2f, trajectory %zu..%zu (%zu samples)\n", fs, block, mode.c_str(), double(f0), settle, trajEnd, dropN);
    const double binHz = fs / double(L);
    for (const std::size_t start : {trajEnd + 64, trajEnd + 64 + L}) {
        std::vector<double> p = powerSpectrum(L_.data() + start, L);
        std::vector<char> ex(L / 2 + 1, 0);
        for (std::size_t k = 0; k <= 8; ++k) ex[k] = 1;
        for (int n = 1; double(n) * f0 < fs * 0.5; ++n) { const double c = double(n) * f0 / binHz; for (std::size_t k = std::size_t(std::max(0.0, std::ceil(c - 8))); double(k) <= std::min(double(L / 2), std::floor(c + 8)); ++k) ex[k] = 1; }
        double al = 0, tot = 0; std::vector<std::pair<double, std::size_t>> peaks;
        for (std::size_t k = 1; k <= L / 2; ++k) { tot += p[k]; if (!ex[k]) { al += p[k]; peaks.push_back({p[k], k}); } }
        std::sort(peaks.rbegin(), peaks.rend());
        std::printf("  frame at +%zu after trajEnd: aliased %.2f dBFS, total %.2f dBFS\n", start - trajEnd, db10(al / 0.5), db10(tot / 0.5));
        std::printf("    top non-excluded bins:");
        for (std::size_t i = 0; i < 8 && i < peaks.size(); ++i) std::printf(" %.1f Hz %.1f dB (%.2f x f0)%s", double(peaks[i].second) * binHz, db10(peaks[i].first / 0.5), double(peaks[i].second) * binHz / f0, i + 1 < 8 ? ";" : "");
        std::printf("\n");
    }
    // Per-partial amplitude change (Goertzel at k*f0 over 4 s windows before the event and after trajEnd + 64)
    {
        const std::size_t W = std::size_t(std::llround(fs / double(f0) * std::floor(0.9 * double(f0)))); // integer periods, ~0.9 s
        auto amp = [&](std::size_t from, double hz) { double re = 0, im = 0; for (std::size_t k = 0; k < W; ++k) { const double a = kTau2 * hz * double(k) / fs; re += double(L_[from + k]) * std::cos(a); im -= double(L_[from + k]) * std::sin(a); } return 2.0 * std::hypot(re, im) / double(W); };
        std::printf("  per-partial dB change (after - before), k: ");
        double worst = 0; int kw = 0; double sumB = 0, sumA = 0;
        for (int k = 1; k <= 64 && double(k) * f0 < fs * 0.45; ++k) { const double b = amp(settle - W, double(k) * f0), a = amp(trajEnd + 64, double(k) * f0); sumB += b * b; sumA += a * a; const double d = 20 * std::log10((a + 1e-12) / (b + 1e-12)); if (b > 1e-4 && std::abs(d) > std::abs(worst)) { worst = d; kw = k; } if (k <= 12 || std::abs(d) > 3) std::printf("%d:%+.1f ", k, d); }
        std::printf("| worst k%d %+.1f dB; harmonic energy %+.2f dB\n", kw, worst, 10 * std::log10(sumA / sumB));
    }
    // RMS trace: 10 consecutive 4096-sample windows after trajEnd, to see whether anything decays
    std::printf("  rms dBFS per 4096 after trajEnd:");
    for (int w = 0; w < 10; ++w) { double a = 0; for (std::size_t k = 0; k < 4096; ++k) { const double v = L_[trajEnd + 64 + std::size_t(w) * 4096 + k]; a += v * v; } std::printf(" %.2f", 10 * std::log10(a / 4096 + 1e-30)); }
    std::printf("\n");
    return 0;
}
