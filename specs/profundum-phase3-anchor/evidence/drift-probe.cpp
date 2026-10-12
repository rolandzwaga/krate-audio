// SC-003(e)(ii) C3 red (T023, 2026-10-12): where does the 0.59 deg 60 s drift between the Anchor and the
// Fold-100 output's h1 come from? The Anchor-vs-core drift is 0.03 deg, so the walk sits between the core's
// output h1 and the folded h1. Three measurements per note, 48 kHz, block 512, BH7-windowed single-bin DFT
// over ceil(f0) periods (the chain test's lockWindowLength), centred at 1 s and 60 s:
//   (A) core inter-partial phase walk: phi_n = theta_n - n*theta_1 for n = 2..6, drift(60 s) - (1 s)
//   (B) folded h1 phase minus core h1 phase, drift
//   (C) control: the SAME Fold fed a perfectly periodic double-synthesised harmonic series (phases fixed),
//       folded h1 phase minus input h1 phase, drift  -> the Fold's own contribution on a stationary input
//   (D) control 2: the core's own h1 vs an ideal double phasor, drift (absolute core h1 walk)
#include <krate/dsp/processors/harmonic_fold.h>
#include <krate/dsp/systems/profundum_core.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
#include <xmmintrin.h>
#include <pmmintrin.h>

using namespace Krate::DSP;
constexpr double kTau2 = 6.283185307179586;
constexpr std::array<double, 7> kBh7{0.27105140069342, 0.43329793923448, 0.21812299954311, 0.06592544638803,
                                     0.01081174209837, 0.00077658482522, 0.00001388721735};
static double phaseAt(const float* x, std::size_t L, double fs, double hz) {
    double re = 0, im = 0;
    for (std::size_t k = 0; k < L; ++k) {
        double w = 0;
        for (std::size_t j = 0; j < 7; ++j) w += ((j % 2) ? -1.0 : 1.0) * kBh7[j] * std::cos(kTau2 * j * k / double(L));
        const double v = double(x[k]) * w, ph = kTau2 * hz * double(k) / fs;
        re += v * std::cos(ph); im -= v * std::sin(ph);
    }
    return std::atan2(im, re);
}
static double wrapDeg(double d) { while (d > 180) d -= 360; while (d < -180) d += 360; return d; }
static float midiHz(int m) { return float(440.0 * std::exp2((double(m) - 69.0) / 12.0)); }

int main(int argc, char** argv) {
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON); _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
    const double fs = 48000.0; const std::size_t block = 512; const double T = argc > 2 ? std::atof(argv[2]) : 60.0;
    const int midi = argc > 1 ? std::atoi(argv[1]) : 48;
    const float f0 = midiHz(midi); const double hz = f0;
    const std::size_t L = std::size_t(std::llround(std::ceil(hz) * fs / hz));
    const std::size_t total = std::size_t(std::llround((T + 1.0) * fs));
    auto centred = [&](double t) { return std::size_t(std::llround(t * fs)) - L / 2; };
    const std::size_t s1 = centred(1.0), sT = centred(T);

    // --- core (mid grid = defaults, excitation 1/1/1) ---
    std::vector<float> core(total, 0.f), coreR(total, 0.f);
    {
        auto c = std::make_unique<ProfundumCore>(); c->prepare(fs);
        c->setControls(SpectralShapeRecipe::Controls{});
        c->setExcitation(ProfundumCore::ExcitationControls{.mix = 1.0f, .drive = 1.0f, .asymmetry = 1.0f});
        c->setRetriggerPhase(ProfundumCore::RetriggerPhase::Reset); c->noteOn(f0);
        for (std::size_t p = 0; p < total; p += block) c->processBlock(core.data() + p, coreR.data() + p, std::min(block, total - p));
    }
    // --- fold the core ---
    std::vector<float> folded = core, foldedR = core;
    { auto f = std::make_unique<HarmonicFold>(); f->prepare(fs, 4096); f->setAmount(100.f); f->setFundamentalHz(f0);
      for (std::size_t p = 0; p < total; p += block) f->processBlock(folded.data() + p, foldedR.data() + p, std::min(block, total - p)); }
    // --- control: periodic synthetic input with the core's 1 s harmonic amplitudes and phases, double phasors ---
    std::vector<float> synth(total, 0.f);
    {
        // measure the core's first 12 harmonic amplitudes/phases at 1 s, re-synthesise them exactly periodic
        std::array<double, 12> amp{}, ph{};
        for (int n = 1; n <= 12; ++n) {
            double re = 0, im = 0;
            for (std::size_t k = 0; k < L; ++k) {
                double w = 0; for (std::size_t j = 0; j < 7; ++j) w += ((j % 2) ? -1.0 : 1.0) * kBh7[j] * std::cos(kTau2 * j * k / double(L));
                const double v = double(core[s1 + k]) * w, a = kTau2 * n * hz * double(k) / fs; re += v * std::cos(a); im -= v * std::sin(a);
            }
            double wsum = 0; for (std::size_t k = 0; k < L; ++k) { double w = 0; for (std::size_t j = 0; j < 7; ++j) w += ((j % 2) ? -1.0 : 1.0) * kBh7[j] * std::cos(kTau2 * j * k / double(L)); wsum += w; }
            amp[n - 1] = 2.0 * std::hypot(re, im) / wsum; ph[n - 1] = std::atan2(im, re) - kTau2 * n * hz * double(s1) / fs;
        }
        for (std::size_t k = 0; k < total; ++k) { double v = 0; for (int n = 1; n <= 12; ++n) v += amp[n - 1] * std::cos(kTau2 * n * hz * double(k) / fs + ph[n - 1]); synth[k] = float(v); }
    }
    std::vector<float> synthF = synth, synthFR = synth;
    { auto f = std::make_unique<HarmonicFold>(); f->prepare(fs, 4096); f->setAmount(100.f); f->setFundamentalHz(f0);
      for (std::size_t p = 0; p < total; p += block) f->processBlock(synthF.data() + p, synthFR.data() + p, std::min(block, total - p)); }

    auto deg = [](double r) { return r * 180.0 / 3.141592653589793; };
    std::printf("midi %d f0 %.4f Hz, T %.0f s, window %zu samples\n", midi, hz, T, L);
    // (D) absolute core h1 vs ideal phasor (phase at window start referenced to t = 0)
    auto absH1 = [&](const std::vector<float>& x, std::size_t s) { return wrapDeg(deg(phaseAt(x.data() + s, L, fs, hz)) + 360.0 * hz * double(s) / fs); };
    const double d1 = absH1(core, s1), dT = absH1(core, sT);
    std::printf("(D) core h1 vs ideal phasor: 1 s %+.4f deg, %.0f s %+.4f deg, drift %+.4f deg\n", d1, T, dT, wrapDeg(dT - d1));
    // (A) inter-partial
    for (int n = 2; n <= 6; ++n) {
        auto rel = [&](std::size_t s) { const double t1 = phaseAt(core.data() + s, L, fs, hz), tn = phaseAt(core.data() + s, L, fs, n * hz); return wrapDeg(deg(tn - n * t1)); };
        const double r1 = rel(s1), rT = rel(sT);
        std::printf("(A) core h%d - %d*h1: 1 s %+.4f deg, %.0f s %+.4f deg, drift %+.4f deg\n", n, n, r1, T, rT, wrapDeg(rT - r1));
    }
    // (B) folded h1 vs core h1
    auto relFold = [&](const std::vector<float>& f, const std::vector<float>& in, std::size_t s) { return wrapDeg(deg(phaseAt(f.data() + s, L, fs, hz) - phaseAt(in.data() + s, L, fs, hz))); };
    const double b1 = relFold(folded, core, s1), bT = relFold(folded, core, sT);
    std::printf("(B) Fold100(core) h1 - core h1: 1 s %+.4f deg, %.0f s %+.4f deg, drift %+.4f deg\n", b1, T, bT, wrapDeg(bT - b1));
    const double c1 = relFold(synthF, synth, s1), cT = relFold(synthF, synth, sT);
    std::printf("(C) Fold100(periodic synth) h1 - synth h1: 1 s %+.4f deg, %.0f s %+.4f deg, drift %+.4f deg\n", c1, T, cT, wrapDeg(cT - c1));
    // RMS sanity
    auto rms = [&](const std::vector<float>& x, std::size_t s) { double a = 0; for (std::size_t k = 0; k < L; ++k) a += double(x[s + k]) * x[s + k]; return 20 * std::log10(std::sqrt(a / L)); };
    std::printf("rms dBFS: core %.2f/%.2f, synth %.2f/%.2f, folded %.2f/%.2f\n", rms(core, s1), rms(core, sT), rms(synth, s1), rms(synth, sT), rms(folded, s1), rms(folded, sT));
    return 0;
}
