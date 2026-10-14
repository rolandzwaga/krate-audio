// =============================================================================
// Profundum Phase 4 — SC-028: the core's per-partial spectrum is preserved across a Drop
// (specs/profundum-phase4-drop-note-behaviour, user ruling 2026-10-13).
//
// Background: the bank's MCF oscillators keep (s, c) across a coefficient change, which
// re-rolls each partial's amplitude by up to ±11 dB after a 48-semitone Drop (main-loop
// probe, evidence/growl-probe.md). ProfundumCore now enables the bank's opt-in
// amplitude- and phase-continuous frequency updates. This TU measures the core alone
// through a Drop-shaped f0PerSample trajectory: every partial's amplitude after the
// landing must equal its amplitude before the Drop. The positive control (the flag off)
// lives in harmonic_oscillator_bank_tests.cpp (HarmonicOscillatorBank_ContinuousFrequencyUpdates).
// =============================================================================

#include <krate/dsp/processors/spectral_shape_recipe.h>
#include <krate/dsp/systems/profundum_core.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Krate::DSP;

namespace {

constexpr double kTwoPiD = 6.283185307179586;

float midiHz(int m) { return static_cast<float>(440.0 * std::exp2((static_cast<double>(m) - 69.0) / 12.0)); }

/// Amplitude of the sinusoid at hz over an integer-period window starting at `from`.
double partialAmplitude(const std::vector<float>& x, std::size_t from, std::size_t len, double fs, double hz) {
    double re = 0.0;
    double im = 0.0;
    for (std::size_t k = 0; k < len; ++k) {
        const double a = kTwoPiD * hz * static_cast<double>(k) / fs;
        re += static_cast<double>(x[from + k]) * std::cos(a);
        im -= static_cast<double>(x[from + k]) * std::sin(a);
    }
    return 2.0 * std::hypot(re, im) / static_cast<double>(len);
}

struct Recipe {
    const char* name;
    SpectralShapeRecipe::Controls controls;
};

}  // namespace

TEST_CASE("ProfundumCore_SpectrumPreservedAcrossDrop", "[systems][profundum][note-behaviour][pitch-continuity]") {
    const std::array<Recipe, 3> recipes{{{"kGrowl", SpectralShapeRecipe::kGrowl},
                                         {"kSawAnchor", SpectralShapeRecipe::kSawAnchor},
                                         {"mid grid", SpectralShapeRecipe::Controls{}}}};
    double worstPartialDb = 0.0;
    double worstEnergyDb = 0.0;
    std::string worstCell;
    int cells = 0;
    for (const double fs : {44100.0, 48000.0, 96000.0}) {
        for (const int midi : {28, 48}) {   // E1, C3
            const float f0 = midiHz(midi);
            const auto f0d = static_cast<double>(f0);
            // Integer-period analysis window of about 0.9 s.
            const auto win = static_cast<std::size_t>(std::llround(fs / f0d * std::floor(0.9 * f0d)));
            const auto settle = static_cast<std::size_t>(fs);   // 1 s at the note before the Drop
            for (const Recipe& r : recipes) {
                for (const double amount : {12.0, 24.0, 48.0}) {
                    for (const double timeMs : {1.0, 60.0, 1000.0}) {
                        const auto dropN = static_cast<std::size_t>(std::llround(timeMs * 0.001 * fs));
                        const std::size_t trajEnd = settle + dropN;
                        const std::size_t after = trajEnd + 4096;   // the mask has returned (Δmax per 32 samples)
                        const std::size_t total = after + win + 64;
                        std::vector<float> traj(total, f0);
                        for (std::size_t n = 0; n < dropN; ++n) {
                            const double u = static_cast<double>(n) / static_cast<double>(dropN);
                            const double d = amount * std::exp(-8.0 * u);   // the exponential family, k = 8
                            traj[settle + n] = static_cast<float>(f0d * std::exp2(d / 12.0));
                        }
                        std::vector<float> left(total, 0.0f);
                        std::vector<float> right(total, 0.0f);
                        auto core = std::make_unique<ProfundumCore>();
                        core->prepare(fs);
                        core->setControls(r.controls);
                        core->setRetriggerPhase(ProfundumCore::RetriggerPhase::Reset);
                        core->noteOn(f0);
                        constexpr std::size_t kBlock = 512;
                        for (std::size_t p = 0; p < total; p += kBlock) {
                            const std::size_t n = std::min(kBlock, total - p);
                            core->processBlock(left.data() + p, right.data() + p, n, traj.data() + p);
                        }
                        ++cells;
                        double energyBefore = 0.0;
                        double energyAfter = 0.0;
                        double cellWorst = 0.0;
                        int cellWorstK = 0;
                        double cellWorstBar = 0.1;
                        double cellWorstLevel = 0.0;
                        double h1 = 0.0;
                        for (int k = 1; k <= 64 && static_cast<double>(k) * f0d < 0.45 * fs; ++k) {
                            const double hz = static_cast<double>(k) * f0d;
                            const double b = partialAmplitude(left, settle - win, win, fs, hz);
                            const double a = partialAmplitude(left, after, win, fs, hz);
                            if (k == 1)
                                h1 = b;
                            energyBefore += b * b;
                            energyAfter += a * a;
                            if (b < h1 * 1e-3)   // −60 dB re h1: below the floor the recipe delivers
                                continue;
                            const double dDb = 20.0 * std::log10(a / b);
                            // Bar: 0.1 dB for partials within 40 dB of h1; 0.5 dB between −40 and −60 dB re h1, where a
                            // 0.9 s single-bin estimate carries leakage from partials up to 60 dB louder.
                            const double levelDb = 20.0 * std::log10(b / h1);
                            const double bar = levelDb >= -40.0 ? 0.1 : 0.5;
                            if (std::abs(dDb) / bar > std::abs(cellWorst) / cellWorstBar) {
                                cellWorst = dDb;
                                cellWorstK = k;
                                cellWorstBar = bar;
                                cellWorstLevel = levelDb;
                            }
                        }
                        REQUIRE(energyBefore > 0.0);
                        const double energyDb = 10.0 * std::log10(energyAfter / energyBefore);
                        INFO(r.name << " midi " << midi << " fs " << fs << " A " << amount << " T " << timeMs
                                    << " ms: worst partial k" << cellWorstK << " (" << cellWorstLevel << " dB re h1) " << cellWorst << " dB (bar " << cellWorstBar << "), energy "
                                    << energyDb << " dB");
                        CHECK(std::abs(cellWorst) <= cellWorstBar);
                        CHECK(std::abs(energyDb) <= 0.05);
                        if (std::abs(cellWorst) > std::abs(worstPartialDb)) {
                            worstPartialDb = cellWorst;
                            worstCell = std::string(r.name) + " midi " + std::to_string(midi) + " fs "
                                        + std::to_string(static_cast<int>(fs)) + " A " + std::to_string(static_cast<int>(amount))
                                        + " T " + std::to_string(static_cast<int>(timeMs)) + " ms, k" + std::to_string(cellWorstK);
                        }
                        worstEnergyDb = std::max(worstEnergyDb, std::abs(energyDb));
                    }
                }
            }
        }
    }
    std::printf("SC-028 spectrum across Drop: %d cells, worst partial %+.4f dB (bar 0.1 within 40 dB of h1, 0.5 dB below) at %s, worst |energy| %.4f dB (bar 0.05)\n",
                cells, worstPartialDb, worstCell.c_str(), worstEnergyDb);
}
