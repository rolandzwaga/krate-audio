// ==============================================================================
// Layer 3: System Tests - ProfundumCore, non-finite TU
//                                    (specs/profundum-phase1-harmonic-core)
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/profundum-phase1-harmonic-core/spec.md
//            specs/profundum-phase1-harmonic-core/plan.md  (S4.8, S8.1, S8.3)
//            specs/profundum-phase1-harmonic-core/tasks.md (T003 wires this TU,
//                                                           T014 fills it)
//
// SCOPE OF THIS TU (plan S8.1; tags [systems][profundum]): SC-016
//   (NonFiniteInputsRejected), SC-020(d) (PitchTrajectoryNonFinite), FR-002
//   (SanitizeNonFinite), FR-062 (PanNonFiniteSanitised). The ONE Phase 1 TU in
//   the -fno-fast-math block of dsp/tests/CMakeLists.txt: it injects NaN/Inf by
//   bit pattern (std::bit_cast) and checks with detail::isFinite. No [long]
//   case lives here.
// ==============================================================================

#include "profundum_core_test_helpers.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/processors/spectral_shape_recipe.h>
#include <krate/dsp/systems/profundum_core.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

using Krate::DSP::ProfundumCore;
using Krate::DSP::SpectralShapeRecipe;
namespace PT = Krate::DSP::ProfundumTest;

namespace {

using Controls = SpectralShapeRecipe::Controls;
using RP = ProfundumCore::RetriggerPhase;
using PanOffsets = std::array<float, Krate::DSP::kMaxPartials>;

constexpr double kFs = 48000.0;
constexpr float kC2 = 65.41f;
constexpr std::size_t kBlock = ProfundumCore::kControlInterval;   // grid-aligned blocks
constexpr std::size_t kSetupSamples = 4800;        // 0.1 s = 150 control intervals
constexpr double kP0 = static_cast<double>(SpectralShapeRecipe::kPowerTarget);

/// The four injected values, built from their bit patterns (tasks.md T014).
struct BadValue {
    const char* name;
    std::uint32_t bits;
    bool finite;   // expected detail::isFinite verdict
};

constexpr std::array<BadValue, 4> kBadValues{{
    {"NaN", 0x7FC00000u, false},
    {"+Inf", 0x7F800000u, false},
    {"-Inf", 0xFF800000u, false},
    {"denormal", 0x00000001u, true},
}};

float valueOf(const BadValue& b) noexcept { return std::bit_cast<float>(b.bits); }

/// Every injected value is checked against its expected detail::isFinite verdict first.
float checkedValue(const BadValue& b) {
    const float v = valueOf(b);
    INFO("value " << b.name);
    REQUIRE(Krate::DSP::detail::isFinite(v) == b.finite);
    return v;
}

bool sameBits(float a, float b) noexcept { return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b); }

/// One Controls field, by name.
struct ControlField {
    const char* name;
    float Controls::*field;
};

constexpr std::array<ControlField, 6> kFields{{
    {"depth", &Controls::depth},
    {"body", &Controls::body},
    {"bodyCurvature", &Controls::bodyCurvature},
    {"bodyEmphasis", &Controls::bodyEmphasis},
    {"edge", &Controls::edge},
    {"shift", &Controls::shift},
}};

bool allFinite(const std::vector<float>& x) {
    return std::all_of(x.begin(), x.end(), [](float v) { return Krate::DSP::detail::isFinite(v); });
}

bool allFinite(std::span<const float> x) {
    return std::all_of(x.begin(), x.end(), [](float v) { return Krate::DSP::detail::isFinite(v); });
}

struct Out {
    std::vector<float> L, R;
};

/// Renders numSamples in kBlock blocks (the last may be shorter), passing the matching trajectory
/// slice when traj is non-null.
Out renderBlocks(ProfundumCore& core, std::size_t numSamples, const float* traj = nullptr) {
    Out o;
    o.L.assign(numSamples, 0.0f);
    o.R.assign(numSamples, 0.0f);
    for (std::size_t pos = 0; pos < numSamples; pos += kBlock) {
        const std::size_t n = std::min(kBlock, numSamples - pos);
        core.processBlock(o.L.data() + pos, o.R.data() + pos, n, traj != nullptr ? traj + pos : nullptr);
    }
    return o;
}

bool sameRender(const Out& a, const Out& b) {
    return a.L.size() == b.L.size() && a.R.size() == b.R.size()
           && PT::samplesBitEqual(a.L.data(), b.L.data(), a.L.size())
           && PT::samplesBitEqual(a.R.data(), b.R.data(), a.R.size());
}

void requireFiniteOutput(const Out& o) {
    REQUIRE(allFinite(o.L));
    REQUIRE(allFinite(o.R));
}

/// A prepared core sounding C2 at mid grid (Reset policy), rendered 0.1 s so the grid is aligned
/// (4800 = 150·32) and the core is settled at f0 = C2.
std::unique_ptr<ProfundumCore> soundingCore() {
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(kFs);
    const PT::Render r = PT::renderCore(*core, PT::midGrid(), kC2, 0.1, RP::Reset, false, kBlock);
    REQUIRE(r.L.size() == kSetupSamples);
    REQUIRE(core->currentFrequency() == kC2);
    REQUIRE(core->stateFinite());
    return core;
}

/// 5 Hz ±2-semitone vibrato around `centre`, sample i (same law as the main TU).
float vibratoAt(std::size_t i, double fs, float centre) {
    const double semis = 2.0 * std::sin(PT::kTwoPi * 5.0 * static_cast<double>(i) / fs);
    return static_cast<float>(static_cast<double>(centre) * std::exp2(semis / 12.0));
}

}  // namespace

// =============================================================================
// SC-016 / FR-062: NaN, ±Inf and denormal into every input
// =============================================================================

TEST_CASE("ProfundumCore_NonFiniteInputsRejected", "[systems][profundum]") {
    // ---- setFrequency ----
    for (const BadValue& bad : kBadValues) {
        INFO("setFrequency(" << bad.name << ")");
        const float v = checkedValue(bad);
        auto a = soundingCore();
        auto twin = soundingCore();
        const float before = a->currentFrequency();
        a->setFrequency(v);

        const Out first = renderBlocks(*a, kBlock);
        if (bad.finite) {
            REQUIRE(a->currentFrequency() == ProfundumCore::kMinF0Hz);   // denormal clamps to 8 Hz
        } else {
            REQUIRE(sameBits(a->currentFrequency(), before));
            REQUIRE(sameRender(first, renderBlocks(*twin, kBlock)));
        }
        const Out rest = renderBlocks(*a, kSetupSamples);
        requireFiniteOutput(first);
        requireFiniteOutput(rest);
        REQUIRE(a->stateFinite());
        if (!bad.finite) {
            REQUIRE(sameBits(a->currentFrequency(), before));
            REQUIRE(sameRender(rest, renderBlocks(*twin, kSetupSamples)));
        }
    }
    {
        INFO("setFrequency(negative)");
        auto a = soundingCore();
        a->setFrequency(-55.0f);
        const Out o = renderBlocks(*a, kBlock);
        REQUIRE(a->currentFrequency() == ProfundumCore::kMinF0Hz);
        requireFiniteOutput(o);
        REQUIRE(a->stateFinite());
    }

    // ---- noteOn ----
    // A non-finite noteOn f0 keeps the held f0 (C2), so it renders exactly like noteOn(C2).
    for (const BadValue& bad : kBadValues) {
        INFO("noteOn(" << bad.name << ")");
        const float v = checkedValue(bad);
        auto a = soundingCore();
        auto twin = soundingCore();
        const float before = a->currentFrequency();
        a->noteOn(v);
        twin->noteOn(kC2);

        const Out o = renderBlocks(*a, kSetupSamples);
        requireFiniteOutput(o);
        REQUIRE(a->stateFinite());
        if (bad.finite) {
            REQUIRE(a->currentFrequency() == ProfundumCore::kMinF0Hz);
        } else {
            REQUIRE(sameBits(a->currentFrequency(), before));
            REQUIRE(sameRender(o, renderBlocks(*twin, kSetupSamples)));
        }
    }
    {
        INFO("noteOn(negative)");
        auto a = soundingCore();
        a->noteOn(-55.0f);
        const Out o = renderBlocks(*a, kBlock);
        REQUIRE(a->currentFrequency() == ProfundumCore::kMinF0Hz);
        requireFiniteOutput(o);
        REQUIRE(a->stateFinite());
    }

    // ---- setControls, each of the six fields ----
    // The other fields equal the pending (mid-grid) values, so a rejected field makes the call a
    // no-op: the render is memcmp-equal to a twin that never received it.
    for (const ControlField& f : kFields) {
        for (const BadValue& bad : kBadValues) {
            INFO("setControls." << f.name << " = " << bad.name);
            const float v = checkedValue(bad);
            auto a = soundingCore();
            auto twin = soundingCore();
            Controls c = PT::midGrid();
            c.*(f.field) = v;
            a->setControls(c);

            const Out o = renderBlocks(*a, kSetupSamples);
            requireFiniteOutput(o);
            REQUIRE(a->stateFinite());
            if (!bad.finite)
                REQUIRE(sameRender(o, renderBlocks(*twin, kSetupSamples)));
        }
    }

    // ---- a pan-offset element ----
    for (const BadValue& bad : kBadValues) {
        INFO("pan offset element = " << bad.name);
        const float v = checkedValue(bad);
        auto a = soundingCore();
        auto twin = soundingCore();
        PanOffsets p{};
        p[3] = v;
        a->setPartialPanOffsets(p);

        const Out o = renderBlocks(*a, kSetupSamples);
        requireFiniteOutput(o);
        REQUIRE(a->stateFinite());
        if (!bad.finite) {
            // Sanitised to 0.0f: the vector is all-zero, the Anchor path stays bit-identical L/R.
            PT::requireLREqual(o.L.data(), o.R.data(), o.L.size());
            REQUIRE(sameRender(o, renderBlocks(*twin, kSetupSamples)));
        }
    }

    // ---- f0PerSample elements ----
    // Bad values at a control update (0), a pitch update (16) and the last element (which becomes
    // the held scalar, FR-043).
    for (const BadValue& bad : kBadValues) {
        INFO("f0PerSample element = " << bad.name);
        const float v = checkedValue(bad);
        auto a = soundingCore();
        auto twin = soundingCore();
        std::vector<float> clean(kSetupSamples, kC2);
        std::vector<float> traj = clean;
        traj[0] = v;
        traj[16] = v;
        traj[kSetupSamples - 1] = v;

        const Out o = renderBlocks(*a, kSetupSamples, traj.data());
        requireFiniteOutput(o);
        REQUIRE(a->stateFinite());
        const Out after = renderBlocks(*a, kBlock);   // nullptr: renders the held scalar
        requireFiniteOutput(after);
        REQUIRE(a->stateFinite());
        if (bad.finite) {
            // The last element clamped to 8 Hz became the held scalar.
            REQUIRE(a->currentFrequency() == ProfundumCore::kMinF0Hz);
        } else {
            REQUIRE(a->currentFrequency() == kC2);
            REQUIRE(sameRender(o, renderBlocks(*twin, kSetupSamples, clean.data())));
            REQUIRE(sameRender(after, renderBlocks(*twin, kBlock)));
        }
    }

    // ---- 8 Hz floor and the clamp ceiling ----
    {
        INFO("8 Hz");
        auto core = std::make_unique<ProfundumCore>();
        core->prepare(kFs);
        const PT::Render r = PT::renderCore(*core, PT::midGrid(), ProfundumCore::kMinF0Hz, 0.5);
        REQUIRE(core->currentFrequency() == ProfundumCore::kMinF0Hz);
        REQUIRE(allFinite(r.L));
        REQUIRE(allFinite(r.R));
        REQUIRE(core->stateFinite());
        REQUIRE(std::any_of(r.L.begin(), r.L.end(), [](float x) { return x != 0.0f; }));
    }
    {
        INFO("clamp ceiling (noteOn(1e6) clamps to capFrequency)");
        auto core = std::make_unique<ProfundumCore>();
        core->prepare(kFs);
        const PT::Render r = PT::renderCore(*core, PT::midGrid(), 1.0e6f, 0.2);
        REQUIRE(core->currentFrequency() == SpectralShapeRecipe::capFrequency(kFs));
        REQUIRE(allFinite(r.L));
        REQUIRE(allFinite(r.R));
        REQUIRE(core->stateFinite());

        const auto d = core->deliveredGains();
        REQUIRE(allFinite(d));
        for (std::size_t k = 1; k < d.size(); ++k) {
            INFO("harmonic " << (k + 1));
            REQUIRE(d[k] == 0.0f);   // h1-only
        }
        const double a1sq = static_cast<double>(d[0]) * static_cast<double>(d[0]);
        const double rel = std::abs(a1sq - kP0) / kP0;
        INFO("|a1^2 - P0| / P0 = " << rel);
        REQUIRE(rel <= 1e-4);
    }
}

// =============================================================================
// SC-020(d): NaN/Inf elements inside a trajectory hold the previous f0
// =============================================================================
//
// FR-062: a non-finite element holds the PREVIOUS SAMPLE's f0 (recursively, so a run of bad
// elements holds the last good element before it), even though the core only samples the
// trajectory every U-th sample (FR-043). The twin therefore replaces each bad element with the
// element before it. The bad indices {16, 4800, 9600..9663} are all on or inside the sampled grid
// (16 = U, 4800 = 150·32, 9600 = 300·32), so every one of them is consumed by a pitch or control
// update, and the element before each differs from the previous sampled element (vibrato).

TEST_CASE("ProfundumCore_PitchTrajectoryNonFinite", "[systems][profundum]") {
    constexpr std::size_t kU = ProfundumCore::kPitchUpdateInterval;
    const std::size_t total = static_cast<std::size_t>(kFs);   // 1 s

    std::vector<float> vibrato(total);
    for (std::size_t i = 0; i < total; ++i)
        vibrato[i] = vibratoAt(i, kFs, kC2);

    const float nanValue = checkedValue(kBadValues[0]);
    const float infValue = checkedValue(kBadValues[1]);
    std::vector<float> bad = vibrato;
    bad[16] = nanValue;
    bad[4800] = nanValue;
    for (std::size_t i = 9600; i <= 9663; ++i)
        bad[i] = infValue;

    std::vector<float> held = bad;
    for (std::size_t i = 0; i < total; ++i) {
        if (Krate::DSP::detail::isFinite(bad[i]))
            continue;
        REQUIRE(i >= 1);
        held[i] = held[i - 1];
    }
    REQUIRE(allFinite(held));

    // Non-vacuity of the per-sample rule: at each bad sampled index the previous sample's f0 is not
    // the previous sampled element's f0 (the decimated hold a sample-and-hold core would give).
    REQUIRE_FALSE(sameBits(held[16], vibrato[0]));
    REQUIRE_FALSE(sameBits(held[4800], vibrato[4800 - kU]));
    REQUIRE_FALSE(sameBits(held[9600], vibrato[9600 - kU]));

    {
        INFO("direct: a bad sampled element holds the sample before it, not the last sampled one");
        auto core = std::make_unique<ProfundumCore>();
        core->prepare(kFs);
        core->setControls(PT::midGrid());
        core->noteOn(100.0f);
        std::array<float, kU + 1> traj{};
        traj.fill(100.0f);
        traj[kU - 1] = 200.0f;     // the previous sample
        traj[kU] = nanValue;       // sampled by the pitch update at U
        std::array<float, kU + 1> L{};
        std::array<float, kU + 1> R{};
        core->processBlock(L.data(), R.data(), traj.size(), traj.data());
        REQUIRE(core->currentFrequency() == 200.0f);
    }
    {
        INFO("direct: a bad last element hands the previous sample's f0 to the scalar (FR-043)");
        auto core = std::make_unique<ProfundumCore>();
        core->prepare(kFs);
        core->setControls(PT::midGrid());
        core->noteOn(100.0f);
        std::array<float, kU + 4> traj{};
        traj.fill(100.0f);
        traj[kU + 2] = 300.0f;     // never sampled: the next sampled index is 2U
        traj[kU + 3] = infValue;   // the block's last element
        std::array<float, 2 * kU> L{};
        std::array<float, 2 * kU> R{};
        core->processBlock(L.data(), R.data(), traj.size(), traj.data());
        REQUIRE(core->currentFrequency() == 100.0f);   // sampled at 0 and U only
        core->processBlock(L.data(), R.data(), L.size(), nullptr);   // crosses 2U: samples the scalar
        REQUIRE(core->currentFrequency() == 300.0f);
    }

    for (const std::size_t block : {std::size_t{64}, std::size_t{37}}) {
        INFO("block " << block);
        auto a = std::make_unique<ProfundumCore>();
        auto twin = std::make_unique<ProfundumCore>();
        auto plain = std::make_unique<ProfundumCore>();
        a->prepare(kFs);
        twin->prepare(kFs);
        plain->prepare(kFs);

        const PT::Render ra = PT::renderCore(*a, PT::midGrid(), vibrato[0], 1.0, RP::Reset, false, block, bad.data());
        const PT::Render rt =
            PT::renderCore(*twin, PT::midGrid(), vibrato[0], 1.0, RP::Reset, false, block, held.data());
        const PT::Render rp =
            PT::renderCore(*plain, PT::midGrid(), vibrato[0], 1.0, RP::Reset, false, block, vibrato.data());

        REQUIRE(allFinite(ra.L));
        REQUIRE(allFinite(ra.R));
        REQUIRE(a->stateFinite());
        REQUIRE(Krate::DSP::detail::isFinite(a->currentFrequency()));
        PT::requireLREqual(ra.L.data(), ra.R.data(), ra.L.size());

        REQUIRE(ra.L.size() == rt.L.size());
        REQUIRE(PT::samplesBitEqual(ra.L.data(), rt.L.data(), ra.L.size()));
        REQUIRE(PT::samplesBitEqual(ra.R.data(), rt.R.data(), ra.R.size()));

        // Non-vacuity: the bad elements were consumed (the hold differs from the clean vibrato).
        REQUIRE(rp.L.size() == ra.L.size());
        REQUIRE(!PT::samplesBitEqual(ra.L.data(), rp.L.data(), ra.L.size()));
    }
}

// =============================================================================
// FR-002: SpectralShapeRecipe::sanitize replaces a non-finite field with the default
// =============================================================================

TEST_CASE("SpectralShapeRecipe_SanitizeNonFinite", "[systems][profundum]") {
    // Every field of kBodyHollow is in range and differs from kDefaultControls, so a substituted
    // default and an unchanged neighbour are both visible.
    const Controls base = SpectralShapeRecipe::kBodyHollow;
    const Controls& def = SpectralShapeRecipe::kDefaultControls;
    for (const ControlField& f : kFields)
        REQUIRE_FALSE(sameBits(base.*(f.field), def.*(f.field)));

    std::array<float, Krate::DSP::kMaxPartials> out{};
    for (std::size_t bi = 0; bi < 3; ++bi) {   // NaN, +Inf, -Inf
        const BadValue& bad = kBadValues[bi];
        const float v = checkedValue(bad);
        REQUIRE_FALSE(bad.finite);
        for (const ControlField& f : kFields) {
            INFO(f.name << " = " << bad.name);
            Controls c = base;
            c.*(f.field) = v;

            const Controls s = SpectralShapeRecipe::sanitize(c);
            for (const ControlField& g : kFields) {
                INFO("checking " << g.name);
                if (g.field == f.field)
                    REQUIRE(sameBits(s.*(g.field), def.*(g.field)));
                else
                    REQUIRE(sameBits(s.*(g.field), base.*(g.field)));
            }

            out.fill(0.0f);
            SpectralShapeRecipe::evaluate(c, kC2, kFs, out);
            REQUIRE(allFinite(std::span<const float>(out.data(), out.size())));
        }

        // Every field non-finite at once.
        INFO("all fields = " << bad.name);
        Controls all = base;
        for (const ControlField& f : kFields)
            all.*(f.field) = v;
        out.fill(0.0f);
        SpectralShapeRecipe::evaluate(all, kC2, kFs, out);
        REQUIRE(allFinite(std::span<const float>(out.data(), out.size())));
    }

    // A non-finite or <= 0 f0Hz passed to evaluate gives a finite vector (T008's documented
    // behaviour: treated as the guard onset).
    std::vector<float> badF0{0.0f, -55.0f};
    for (const BadValue& bad : kBadValues)
        badF0.push_back(checkedValue(bad));
    for (const float f0 : badF0) {
        INFO("f0 bits " << std::bit_cast<std::uint32_t>(f0));
        out.fill(0.0f);
        SpectralShapeRecipe::evaluate(def, f0, kFs, out);
        REQUIRE(allFinite(std::span<const float>(out.data(), out.size())));
        REQUIRE(out[0] > 0.0f);
    }
}

// =============================================================================
// FR-062 / S4.8: a NaN pan element behaves exactly like 0.0f
// =============================================================================

TEST_CASE("ProfundumCore_PanNonFiniteSanitised", "[systems][profundum]") {
    const float nanValue = checkedValue(kBadValues[0]);

    // Non-zero vector: the NaN element must pan like 0.0f while the others still apply.
    {
        INFO("non-zero vector");
        auto a = soundingCore();
        auto twin = soundingCore();
        PanOffsets p{};
        p[1] = 0.25f;
        p[4] = -0.25f;
        PanOffsets q = p;
        p[10] = nanValue;
        q[10] = 0.0f;
        a->setPartialPanOffsets(p);
        twin->setPartialPanOffsets(q);

        const Out oa = renderBlocks(*a, kSetupSamples);
        const Out ot = renderBlocks(*twin, kSetupSamples);
        requireFiniteOutput(oa);
        REQUIRE(a->stateFinite());
        REQUIRE(sameRender(oa, ot));
        // Non-vacuity: the offsets were applied.
        REQUIRE(!PT::samplesBitEqual(oa.L.data(), oa.R.data(), oa.L.size()));
    }

    // All-zero apart from the NaN: identical to an all-zero vector, and L == R.
    {
        INFO("all-zero vector");
        auto a = soundingCore();
        auto twin = soundingCore();
        PanOffsets p{};
        p[10] = nanValue;
        a->setPartialPanOffsets(p);
        twin->setPartialPanOffsets(PanOffsets{});

        const Out oa = renderBlocks(*a, kSetupSamples);
        const Out ot = renderBlocks(*twin, kSetupSamples);
        requireFiniteOutput(oa);
        REQUIRE(a->stateFinite());
        REQUIRE(sameRender(oa, ot));
        PT::requireLREqual(oa.L.data(), oa.R.data(), oa.L.size());
    }
}
