// ==============================================================================
// Layer 3: System Tests - ProfundumCore Stiffness and Phase alignment (Profundum Phase 15)
// ==============================================================================
// specs/Profundum-roadmap.md, Phase 15 (rulings 2026-10-27):
//   - Stiffness: partial n at n·f0·√(1 + B·n²)/√(1 + B) within 0.1 cent, h1 exact, B scaled with the note;
//     a stretched partial at or above the cap is removed, never aliased.
//   - Phase: the clean magnitude spectrum unchanged, the target alignment seeded at note-on, a live change
//     slews to it with every partial's frequency offset <= 5 Hz and no click.
//   - Stiffness 0 and Phase 0 render bit-identically to a core that never heard of either.
// ==============================================================================

#include "profundum_core_test_helpers.h"

#include <krate/dsp/processors/spectral_shape_recipe.h>
#include <krate/dsp/systems/profundum_core.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

using namespace Krate::DSP;
namespace PT = Krate::DSP::ProfundumTest;

namespace {

constexpr double kFs = 48000.0;
constexpr double kTestPi = 3.14159265358979323846;

std::size_t samplesAt(double seconds, double fs) { return static_cast<std::size_t>(std::llround(seconds * fs)); }

double wrapHalfCycle(double c) { return c - std::round(c); }

double wrapPi(double x) { return x - 2.0 * kTestPi * std::round(x / (2.0 * kTestPi)); }

/// Frequency of the component near `hz` (Hz): Goertzel phase advance between two 1 s BH7 frames 1 s apart.
double measuredHz(const std::vector<float>& x, std::size_t start, double fs, double hz) {
    const std::size_t frame = samplesAt(1.0, fs);
    REQUIRE(start + 2 * frame <= x.size());
    const double phA = PT::goertzelPhase(x.data() + start, frame, fs, hz);
    const double phB = PT::goertzelPhase(x.data() + start + frame, frame, fs, hz);
    const double nominalCycles = hz;   // 1 s apart
    const double residual = wrapHalfCycle((phB - phA) / PT::kTwoPi - (nominalCycles - std::floor(nominalCycles)));
    return hz + residual;
}

double cents(double measured, double want) { return 1200.0 * std::log2(measured / want); }

/// Renders `seconds` of a Reset-policy note at f0 with the given Stiffness / Phase (set before noteOn).
PT::Render renderWith(ProfundumCore& core, const SpectralShapeRecipe::Controls& c, float f0, double seconds,
                      float stiffness, float phase, bool callSetters = true) {
    if (callSetters) {
        core.setStiffness(stiffness, f0);
        core.setPhaseAlignment(phase);
    }
    return PT::renderCore(core, c, f0, seconds);
}

/// BH7-windowed single-bin DFT power (dB) at hz over a 1 s frame.
double harmonicDb(const std::vector<float>& x, std::size_t start, double fs, double hz) {
    const std::size_t frame = samplesAt(1.0, fs);
    REQUIRE(start + frame <= x.size());
    const std::vector<double> w = PT::bh7Window(frame);
    double re = 0.0;
    double im = 0.0;
    for (std::size_t k = 0; k < frame; ++k) {
        const double v = static_cast<double>(x[start + k]) * w[k];
        const double ph = PT::kTwoPi * hz * static_cast<double>(k) / fs;
        re += v * std::cos(ph);
        im -= v * std::sin(ph);
    }
    return PT::safeDb10(re * re + im * im);
}

double crestDb(const float* x, std::size_t n) {
    double peak = 0.0;
    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        peak = std::max(peak, std::abs(static_cast<double>(x[i])));
        sum += static_cast<double>(x[i]) * static_cast<double>(x[i]);
    }
    return 20.0 * std::log10(peak / std::sqrt(sum / static_cast<double>(n)));
}

double maxStep(const float* x, std::size_t n) {
    double m = 0.0;
    for (std::size_t i = 1; i < n; ++i)
        m = std::max(m, std::abs(static_cast<double>(x[i]) - static_cast<double>(x[i - 1])));
    return m;
}

/// φ_n − n·φ_1 of the rendered sine partials in a 1 s frame at `start` (rad, wrapped): the alignment, independent
/// of where the frame starts.
double relativePhase(const std::vector<float>& x, std::size_t start, double fs, double f0, int n) {
    const std::size_t frame = samplesAt(1.0, fs);
    REQUIRE(start + frame <= x.size());
    // DFT phase of sin(ωt + ψ) is ψ − π/2.
    const double p1 = PT::goertzelPhase(x.data() + start, frame, fs, f0) + kTestPi / 2.0;
    const double pn = PT::goertzelPhase(x.data() + start, frame, fs, f0 * n) + kTestPi / 2.0;
    return wrapPi(pn - static_cast<double>(n) * p1);
}

}  // namespace

// The stretch law itself: h1 exact, monotone in n and B, the B = 0 identity, B scaled with the note.
TEST_CASE("ProfundumCore_StiffnessLaw", "[systems][profundum][phase15]") {
    CHECK(ProfundumCore::stretchedRatio(0.0, 7) == 7.0);
    CHECK(ProfundumCore::stretchedRatio(0.02, 1) == 1.0);
    double prev = 1.0;
    for (int n = 2; n <= 64; ++n) {
        const double r = ProfundumCore::stretchedRatio(0.01, n);
        CHECK(r > static_cast<double>(n));
        CHECK(r - prev > 1.0);   // the gaps widen: every partial sharper than the series
        prev = r;
    }
    CHECK(ProfundumCore::stiffnessCoefficient(0.0f, 55.0f) == 0.0);
    CHECK(ProfundumCore::stiffnessCoefficient(1.0f, 55.0f) == ProfundumCore::kMaxStiffnessB);
    CHECK(ProfundumCore::stiffnessCoefficient(1.0f, 110.0f) == 2.0 * ProfundumCore::kMaxStiffnessB);
    CHECK(ProfundumCore::stiffnessCoefficient(0.5f, 55.0f) == 0.25 * ProfundumCore::kMaxStiffnessB);
    CHECK(ProfundumCore::alignmentPhase(1.0f, 1) == 0.0);   // h1 never moves
    CHECK(ProfundumCore::alignmentPhase(0.0f, 9) == 0.0);
    CHECK(std::abs(ProfundumCore::alignmentPhase(1.0f, 9) - kTestPi / 2.0) < 1e-12);
}

// SC: every measured partial within 0.1 cent of the stretch law, h1 exact, at A1 and A2 (B doubles).
TEST_CASE("ProfundumCore_StiffnessPartialFrequencies", "[systems][profundum][phase15]") {
    for (const float f0 : {55.0f, 110.0f}) {
        auto core = std::make_unique<ProfundumCore>();
        core->prepare(kFs);
        const PT::Render r = renderWith(*core, SpectralShapeRecipe::kSawAnchor, f0, 2.25, 1.0f, 0.0f);
        const double b = ProfundumCore::stiffnessCoefficient(1.0f, f0);
        REQUIRE(core->stiffnessB() == b);
        const std::size_t start = samplesAt(0.2, kFs);
        for (const int n : {1, 2, 3, 5, 8}) {
            const double want = ProfundumCore::stretchedRatio(b, n) * static_cast<double>(f0);
            const double got = measuredHz(r.L, start, kFs, want);
            INFO("f0 " << f0 << " n " << n << " want " << want << " Hz, got " << got << " Hz");
            CHECK(std::abs(cents(got, want)) < 0.1);
            if (n > 1)   // audibly stretched: tens of cents sharp of the integer series
                CHECK(cents(want, static_cast<double>(n) * static_cast<double>(f0)) > 5.0);
        }
    }
}

// A stretched partial whose frequency reaches the cap is removed (delivered gain 0); one the integer series
// would have kept proves the mask reads the stretched frequency. The default mask is the identity case.
TEST_CASE("ProfundumCore_StiffnessMaskOnStretchedFrequencies", "[systems][profundum][phase15]") {
    std::array<float, kMaxPartials> rel{};
    for (std::size_t k = 0; k < rel.size(); ++k)
        rel[k] = static_cast<float>(k + 1);
    for (const float f0 : {20.0f, 55.0f, 300.0f}) {
        std::array<float, kMaxPartials> a{};
        std::array<float, kMaxPartials> b{};
        SpectralShapeRecipe::evaluateMask(f0, kFs, std::span<float>(a.data(), 64));
        SpectralShapeRecipe::evaluateMaskStretched(f0, kFs, std::span<const float>(rel.data(), 64),
                                                   std::span<float>(b.data(), 64));
        for (std::size_t k = 0; k < 64; ++k)
            CHECK(std::bit_cast<std::uint32_t>(a[k]) == std::bit_cast<std::uint32_t>(b[k]));
    }

    auto core = std::make_unique<ProfundumCore>();
    core->prepare(kFs);
    const float f0 = 600.0f;   // B = 0.109 at Stiffness 1: partial 12 sits near 20 kHz
    static_cast<void>(renderWith(*core, SpectralShapeRecipe::kSawAnchor, f0, 0.05, 1.0f, 0.0f));
    const double capHz = static_cast<double>(SpectralShapeRecipe::capFrequency(kFs));
    const auto d = core->deliveredGains();
    const double b = core->stiffnessB();
    bool sawRemovedByStretch = false;
    for (int n = 1; n <= core->numPartials(); ++n) {
        const double hz = ProfundumCore::stretchedRatio(b, n) * static_cast<double>(f0);
        INFO("n " << n << " stretched " << hz << " Hz, cap " << capHz);
        if (hz >= capHz) {
            CHECK(d[static_cast<std::size_t>(n - 1)] == 0.0f);
            if (static_cast<double>(n) * static_cast<double>(f0) < capHz)
                sawRemovedByStretch = true;
        }
    }
    CHECK(sawRemovedByStretch);
}

// SC: Stiffness 0 and Phase 0 are bit-transparent against a core whose setters never ran.
TEST_CASE("ProfundumCore_StiffnessPhaseDefaultsBitIdentical", "[systems][profundum][phase15]") {
    for (const float f0 : {41.2f, 110.0f}) {
        auto plain = std::make_unique<ProfundumCore>();
        auto set = std::make_unique<ProfundumCore>();
        plain->prepare(kFs);
        set->prepare(kFs);
        const PT::Render a = renderWith(*plain, SpectralShapeRecipe::kSawAnchor, f0, 0.5, 0.0f, 0.0f, false);
        const PT::Render b = renderWith(*set, SpectralShapeRecipe::kSawAnchor, f0, 0.5, 0.0f, 0.0f, true);
        REQUIRE(a.L.size() == b.L.size());
        std::size_t diffs = 0;
        for (std::size_t i = 0; i < a.L.size(); ++i)
            diffs += std::bit_cast<std::uint32_t>(a.L[i]) != std::bit_cast<std::uint32_t>(b.L[i]) ? 1u : 0u;
        INFO("f0 " << f0);
        CHECK(diffs == 0u);
    }
}

// SC: Phase leaves the clean magnitude spectrum unchanged (every harmonic within 0.1 dB) and applies the alignment
// at note-on; the crest factor rises monotonically from Phase 0 to 1 on the saw spectrum, by >= 4 dB in all.
TEST_CASE("ProfundumCore_PhaseAlignmentSpectrumAndCrest", "[systems][profundum][phase15]") {
    const float f0 = 55.0f;
    const auto& c = SpectralShapeRecipe::kSawAnchor;
    std::vector<double> ref;
    double prevCrest = 0.0;
    double firstCrest = 0.0;
    for (const float x : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        auto core = std::make_unique<ProfundumCore>();
        core->prepare(kFs);
        const PT::Render r = renderWith(*core, c, f0, 1.5, 0.0f, x);
        const std::size_t start = samplesAt(0.25, kFs);
        std::vector<double> levels;
        for (int n = 1; n <= 24; ++n)
            levels.push_back(harmonicDb(r.L, start, kFs, f0 * n));
        if (ref.empty())
            ref = levels;
        for (std::size_t k = 0; k < levels.size(); ++k) {
            INFO("phase " << x << " harmonic " << (k + 1) << ": " << levels[k] << " dB vs " << ref[k]);
            CHECK(std::abs(levels[k] - ref[k]) < 0.1);
        }
        for (const int n : {2, 3, 5, 9}) {
            const double want = ProfundumCore::alignmentPhase(x, n);
            const double got = relativePhase(r.L, start, kFs, f0, n);
            INFO("phase " << x << " n " << n << " want " << want << " got " << got);
            CHECK(std::abs(wrapPi(got - want)) < 0.05);
        }
        const double crest = crestDb(r.L.data() + start, samplesAt(1.0, kFs));
        INFO("phase " << x << " crest " << crest << " dB, previous " << prevCrest);
        if (x == 0.0f)
            firstCrest = crest;
        else
            CHECK(crest > prevCrest);
        prevCrest = crest;
    }
    CHECK(prevCrest - firstCrest >= 4.0);
}

// SC: a live Phase change on a held note slews every partial to the new alignment with |offset| <= 5 Hz, ends in
// the same alignment as a fresh note, and adds no click (largest sample step within 5 % of the steady renders').
TEST_CASE("ProfundumCore_PhaseGlideBoundedAndClickFree", "[systems][profundum][phase15]") {
    const float f0 = 55.0f;
    const auto& c = SpectralShapeRecipe::kSawAnchor;
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(kFs);
    core->setStiffness(0.0f, f0);
    core->setPhaseAlignment(0.0f);
    core->setRetriggerPhase(ProfundumCore::RetriggerPhase::Reset);
    core->setControls(c);
    core->noteOn(f0);
    const std::size_t total = samplesAt(2.0, kFs);
    const std::size_t change = samplesAt(0.5, kFs);
    std::vector<float> L(total, 0.0f);
    std::vector<float> R(total, 0.0f);
    constexpr std::size_t kBlock = 64;
    for (std::size_t pos = 0; pos < total; pos += kBlock) {
        if (pos == change) {
            core->clearMaxPhaseGlideHz();
            core->setPhaseAlignment(1.0f);
            REQUIRE(core->phaseGliding());
        }
        const std::size_t n = std::min(kBlock, total - pos);
        core->processBlock(L.data() + pos, R.data() + pos, n);
    }
    CHECK_FALSE(core->phaseGliding());
    INFO("largest glide offset " << core->maxPhaseGlideHz() << " Hz");
    CHECK(core->maxPhaseGlideHz() > 1.0);    // it did glide
    CHECK(core->maxPhaseGlideHz() <= 5.0 + 1e-9);

    const std::size_t after = samplesAt(0.75, kFs);   // the glide takes <= 100 ms per partial
    for (const int n : {2, 3, 5, 9, 17}) {
        const double want = ProfundumCore::alignmentPhase(1.0f, n);
        const double got = relativePhase(L, after, kFs, f0, n);
        INFO("n " << n << " want " << want << " got " << got);
        CHECK(std::abs(wrapPi(got - want)) < 0.05);
    }

    auto steady0 = std::make_unique<ProfundumCore>();
    auto steady1 = std::make_unique<ProfundumCore>();
    steady0->prepare(kFs);
    steady1->prepare(kFs);
    const PT::Render s0 = renderWith(*steady0, c, f0, 1.0, 0.0f, 0.0f);
    const PT::Render s1 = renderWith(*steady1, c, f0, 1.0, 0.0f, 1.0f);
    const std::size_t skip = samplesAt(0.1, kFs);
    const double steadyStep = std::max(maxStep(s0.L.data() + skip, s0.L.size() - skip),
                                       maxStep(s1.L.data() + skip, s1.L.size() - skip));
    const double glideStep = maxStep(L.data() + change, samplesAt(0.2, kFs));
    INFO("glide max step " << glideStep << ", steady " << steadyStep);
    CHECK(glideStep <= 1.05 * steadyStep);
}

