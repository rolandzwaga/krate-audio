// ==============================================================================
// Test helpers: ProfundumCore excitation render and measurement helpers
//                                    (Profundum Phase 2, specs/profundum-phase2-edge-fold)
// ==============================================================================
// tasks.md T011 "Test helper first"; plan S8.2. Test-side only: allocation is
// fine here.
//
// Every helper is an `inline` function (or function template) in namespace
// Krate::DSP::ProfundumTest, next to the Phase 1 helpers it builds on
// (profundum_core_test_helpers.h, frozen by SC-021). Never an anonymous
// namespace in a header: unused-function warnings would break the
// zero-warning rule.
//
// ODR sweep (2026-10-10, grep -rn over dsp/ plugins/ tests/): renderAnyCore,
// anyCoreSampleRate, renderExcited, goertzelPhasor (exists only in the
// separate namespace Krate::DSP::HarmonicFoldTest), goertzelPhasorWindowed,
// t1Transfer, generatedEvenEnergyDb, aliasSafeFftLength, renderMixZeroTwin -
// no other definition in namespace Krate::DSP::ProfundumTest.
// ==============================================================================

#pragma once

#include "profundum_core_test_helpers.h"
#include "profundum_phase1_core_ref.h"

#include <krate/dsp/processors/spectral_shape_recipe.h>
#include <krate/dsp/systems/profundum_core.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <memory>
#include <vector>

namespace Krate::DSP::ProfundumTest {

/// coreSampleRate (profundum_core_test_helpers.h:69) for any core type that exposes
/// maxShapeStepPerInterval() and kControlInterval (the live core and Phase1Ref::ProfundumCore).
template <class Core>
double anyCoreSampleRate(const Core& core) {
    const double dMax = static_cast<double>(core.maxShapeStepPerInterval());
    REQUIRE(dMax > 0.0);
    const double fs = static_cast<double>(Core::kMaxGainSlewPerSecOverSqrtP0)
                      * std::sqrt(static_cast<double>(SpectralShapeRecipe::kPowerTarget))
                      * static_cast<double>(Core::kControlInterval) / dMax;
    return std::round(fs);
}

/// renderCore (profundum_core_test_helpers.h:90-124) generalised over the core type, so the
/// live ProfundumCore and the pinned Phase1Ref::ProfundumCore see the identical call sequence
/// (SC-004(a)). The caller prepares the core. Sets the policy and controls, calls noteOn(f0), then
/// renders `seconds` in `block`-sized blocks (the last one may be shorter), passing the matching
/// slice of `traj` (which must hold the whole render) when it is non-null.
template <class Core>
Render renderAnyCore(Core& core, const SpectralShapeRecipe::Controls& c, float f0, double seconds,
                     typename Core::RetriggerPhase policy = Core::RetriggerPhase::Reset,
                     std::size_t block = Core::kControlInterval, const float* traj = nullptr) {
    REQUIRE(core.isPrepared());
    REQUIRE(block > 0);
    const double fs = anyCoreSampleRate(core);
    const auto total = static_cast<std::size_t>(std::llround(seconds * fs));

    Render r;
    r.L.assign(total, 0.0f);
    r.R.assign(total, 0.0f);

    core.setRetriggerPhase(policy);
    core.setControls(c);
    core.noteOn(f0);

    for (std::size_t pos = 0; pos < total; pos += block) {
        const std::size_t n = std::min(block, total - pos);
        core.processBlock(r.L.data() + pos, r.R.data() + pos, n, traj != nullptr ? traj + pos : nullptr);
    }
    return r;
}

/// setExcitation(ex), then renderAnyCore's sequence (policy, controls, noteOn, blocks).
inline Render renderExcited(ProfundumCore& core, const SpectralShapeRecipe::Controls& c,
                            const ProfundumCore::ExcitationControls& ex, float f0, double seconds,
                            ProfundumCore::RetriggerPhase policy = ProfundumCore::RetriggerPhase::Reset,
                            std::size_t block = ProfundumCore::kControlInterval,
                            const float* traj = nullptr) {
    core.setExcitation(ex);
    return renderAnyCore(core, c, f0, seconds, policy, block, traj);
}

/// The twin reference: a fresh core prepared at `fs`, driven with the same call sequence as
/// renderExcited but with `mix = 0` (drive and asymmetry kept).
inline Render renderMixZeroTwin(double fs, const SpectralShapeRecipe::Controls& c,
                                const ProfundumCore::ExcitationControls& ex, float f0, double seconds,
                                ProfundumCore::RetriggerPhase policy = ProfundumCore::RetriggerPhase::Reset,
                                std::size_t block = ProfundumCore::kControlInterval,
                                const float* traj = nullptr) {
    auto twin = std::make_unique<ProfundumCore>();
    twin->prepare(fs);
    ProfundumCore::ExcitationControls zero = ex;
    zero.mix = 0.0f;
    return renderExcited(*twin, c, zero, f0, seconds, policy, block, traj);
}

/// BH7-windowed single-bin complex DFT in double at `hz` against a precomputed window `w` of
/// length L (sample 0 = t 0). goertzelPhasor's arithmetic without rebuilding the window.
inline std::complex<double> goertzelPhasorWindowed(const float* x, const std::vector<double>& w,
                                                   std::size_t L, double fs, double hz) {
    REQUIRE(x != nullptr);
    REQUIRE(w.size() >= L);
    double re = 0.0;
    double im = 0.0;
    for (std::size_t k = 0; k < L; ++k) {
        const double v = static_cast<double>(x[k]) * w[k];
        const double ph = kTwoPi * hz * static_cast<double>(k) / fs;
        re += v * std::cos(ph);
        im -= v * std::sin(ph);
    }
    return std::complex<double>(re, im);
}

/// BH7-windowed single-bin complex DFT in double at `hz` (identical body to the
/// HarmonicFoldTest helper, processors/harmonic_fold_test_helpers.h:224).
inline std::complex<double> goertzelPhasor(const float* x, std::size_t L, double fs, double hz) {
    REQUIRE(x != nullptr);
    const std::vector<double> w = bh7Window(L);
    double re = 0.0;
    double im = 0.0;
    for (std::size_t k = 0; k < L; ++k) {
        const double v = static_cast<double>(x[k]) * w[k];
        const double ph = kTwoPi * hz * static_cast<double>(k) / fs;
        re += v * std::cos(ph);
        im -= v * std::sin(ph);
    }
    return std::complex<double>(re, im);
}

/// T1 = output h1 phasor / reference h1 phasor.
inline std::complex<double> t1Transfer(const float* out, const float* ref, std::size_t L, double fs,
                                       double f0) {
    const std::complex<double> hRef = goertzelPhasor(ref, L, fs, f0);
    const std::complex<double> hOut = goertzelPhasor(out, L, fs, f0);
    REQUIRE(std::norm(hRef) > 0.0);
    return hOut / hRef;
}

/// Generated even-harmonic energy in dB re the output's h1 power: Σ over even n with n·f0 < fs/2
/// of |Yn − YnRef|², divided by w1 = |Y1|² of the output (all BH7 phasors over L samples).
inline double generatedEvenEnergyDb(const float* out, const float* ref, std::size_t L, double fs,
                                    double f0) {
    REQUIRE(out != nullptr);
    REQUIRE(ref != nullptr);
    REQUIRE(f0 > 0.0);
    const std::vector<double> w = bh7Window(L);
    const double w1 = std::norm(goertzelPhasorWindowed(out, w, L, fs, f0));
    REQUIRE(w1 > 0.0);
    double even = 0.0;
    for (int n = 2; static_cast<double>(n) * f0 < fs * 0.5; n += 2) {
        const double hz = static_cast<double>(n) * f0;
        even += std::norm(goertzelPhasorWindowed(out, w, L, fs, hz) - goertzelPhasorWindowed(ref, w, L, fs, hz));
    }
    return safeDb10(even / w1);
}

/// The smallest power of two L >= aliasFftLength(fs, f0) such that, for m = 1..4, the distance
/// from m·fs to the nearest multiple of f0 is >= 16·fs/L Hz (folded images then never land on
/// an excluded harmonic window). REQUIREs L <= 2^22.
inline std::size_t aliasSafeFftLength(double fs, double f0) {
    REQUIRE(fs > 0.0);
    REQUIRE(f0 > 0.0);
    constexpr std::size_t kMaxLength = std::size_t{1} << 22;
    const auto safe = [fs, f0](std::size_t L) {
        const double minDistance = 16.0 * fs / static_cast<double>(L);
        for (int m = 1; m <= 4; ++m) {
            const double r = std::fmod(static_cast<double>(m) * fs, f0);
            if (std::min(r, f0 - r) < minDistance)
                return false;
        }
        return true;
    };
    std::size_t L = aliasFftLength(fs, f0);
    while (!safe(L) && L <= kMaxLength)
        L <<= 1;
    REQUIRE(L <= kMaxLength);
    return L;
}

}  // namespace Krate::DSP::ProfundumTest
