// Profundum Phase 13 preset showcase tests (specs/profundum-phase13-presets-release/tasks.md)
//
// Placeholder created by T007 (Deviation 1). Cases written by T018, T023:
// showcase shards (FR-012 - FR-014, FR-043; SC-007), Profundum_ReferenceColumns (SC-008, SC-020 pilot).
//
// T018 (FR-012 - FR-014, FR-043; C-5, C-7; plan 5.4, 5.8, P-2 - P-5, P-7, P-10):
//   - Profundum_ShowcaseArmMechanics (per push): the AT-10 primary arm on a test-local preset (2 arms, E_sub, bar
//     2 dB, both pass, no override); the P-10 endpoint guard's non-vacuity (a declaration that omits A(P) refuses the
//     arm's own render); the primary-override rule (AT-31 claimed by a SOLID preset: primary fails, secondary records
//     "kFieldImageModeId=Spread" and is measured).
//   - Profundum_PresetDistanceMath (per push, pure): P-7 units, the side / mid clamp, masking and F.
//   - Profundum_PresetShowcase_<k>of8 ([long]): the library's primary and secondary columns, written to
//     evidence/coverage-matrix/shard-<k>.csv (merged by evidence/merge-csv.js).

#include <catch2/catch_test_macros.hpp>

#include "preset_render_support.h"
#include "profundum_preset_host.h"
#include "profundum_test_fixture.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace ProfundumPluginTest;

namespace {

using ScCap = ::Profundum::PresetDefs::Capability;

[[nodiscard]] std::vector<std::uint8_t> scBytesOf(const StateBytes& b) {
    std::vector<std::uint8_t> out(b.size());
    if (!b.empty()) {
        std::memcpy(out.data(), b.data(), b.size());
    }
    return out;
}

/// The test-local mechanics preset: endpoint A = B = @p a over a fresh processor's stream (P-6 pins hold by
/// default; Morph Position 0).
[[nodiscard]] std::vector<std::uint8_t> scPresetComp(const Krate::DSP::MorphState& a) {
    auto fresh = std::make_unique<ProcessorFixture>();
    return scBytesOf(withEndpoints(writeV2Stream(*fresh->proc), a, a));
}

/// A(P) of the mechanics preset: A_s with Anchor level 1, Sine, LOCK.
[[nodiscard]] Krate::DSP::MorphState scMechanicsState() {
    Krate::DSP::MorphState a = auRd::auStateAs();
    a.anchor.level = 1.0f;
    a.anchor.waveform = Krate::DSP::AnchorOscillator::Waveform::Sine;
    a.anchor.mode = Krate::DSP::AnchorOscillator::Mode::Lock;
    REQUIRE(Krate::DSP::isValidMorphState(a));
    return a;
}

/// coverage-matrix margin: AT-13 passes on |cents - expected| <= 5, every other column on value >= bar.
[[nodiscard]] double scMargin(const ShowcaseArm& arm) {
    if (arm.column == ScCap::AT13) {
        return auRd::kAuBarCents - std::abs(arm.value - arm.bar);
    }
    return arm.value - arm.bar;
}

[[nodiscard]] std::filesystem::path scCoverageDir() {
    return {std::string(KRATE_DSP_TESTS_DIR) +
                                 "/../../specs/profundum-phase13-presets-release/evidence/coverage-matrix"};
}

/// One Profundum_PresetShowcase shard: every preset index i with i % 8 == k, its primary and every secondary.
void scRunShowcaseShard(std::size_t k) {
    namespace Pd = ::Profundum::PresetDefs;
    enableFTZDAZ();
    const std::vector<Pd::ProfundumPresetDef>& defs = Pd::allPresets();
    REQUIRE(!defs.empty());

    std::ostringstream os;
    os << std::setprecision(6);
    os << "preset,column,role,variant,note,value,bar,margin,window_ms,overrides,descriptor,tap\n";
    std::size_t presets = 0;
    std::size_t armsWritten = 0;
    for (std::size_t i = 0; i < defs.size(); ++i) {
        if (i % 8u != k) {
            continue;
        }
        const Pd::ProfundumPresetDef& def = defs[i];
        std::vector<std::uint8_t> comp;
        std::string why;
        const bool built = buildPresetComponentState(def, comp, why);
        INFO("preset " << def.name << ": " << why);
        REQUIRE(built);
        const DecodedPresetState d = decodePresetState(comp);
        REQUIRE(d.ok);
        ++presets;

        std::vector<std::pair<Pd::Capability, bool>> claims;
        claims.emplace_back(def.primary, true);
        for (const Pd::Capability c : def.secondaries) {
            claims.emplace_back(c, false);
        }
        for (const auto& [column, primary] : claims) {
            const std::string label(Pd::capabilityTable()[static_cast<std::size_t>(column)].label);
            const std::vector<ShowcaseArm> arms = showcaseColumn(d, comp, column, primary);
            INFO("preset " << def.name << ", column " << label << (primary ? " (primary)" : " (secondary)"));
            REQUIRE(!arms.empty());
            for (const ShowcaseArm& arm : arms) {
                os << auRd::auCsvField(std::string(def.name)) << ',' << auRd::auCsvField(label) << ','
                   << (primary ? "primary" : "secondary") << ',' << auRd::auCsvField(arm.variant) << ','
                   << auRd::auNoteName(arm.note) << ',' << arm.value << ',' << arm.bar << ',' << scMargin(arm) << ','
                   << arm.windowMs << ',' << auRd::auCsvField(arm.overrides) << ',' << auRd::auCsvField(arm.descriptor)
                   << ',' << auRd::auCsvField(arm.tap) << '\n';
                ++armsWritten;
                INFO(auRd::auNoteName(arm.note) << ": " << arm.descriptor << " lo " << arm.lo << " hi " << arm.hi
                                                << " value " << arm.value << " bar " << arm.barText << " overrides \""
                                                << arm.overrides << "\"");
                CHECK(arm.measured);
                CHECK(arm.pass);
                CHECK(arm.fidelityPass);
                if (primary) {
                    CHECK(arm.overrides.empty());
                }
            }
        }
    }

    const std::string csv = os.str();
    std::cout << "# Profundum_PresetShowcase_" << k << "of8: presets=" << presets << " arms=" << armsWritten << "\n"
              << csv << std::flush;
    const std::filesystem::path dir = scCoverageDir();
    std::filesystem::create_directories(dir);
    std::ofstream out(dir / ("shard-" + std::to_string(k) + ".csv"), std::ios::binary);
    out << csv;
    CHECK(out.good());
}

}  // namespace

TEST_CASE("Profundum_ShowcaseArmMechanics", "[profundum][showcase]") {
    enableFTZDAZ();
    const Krate::DSP::MorphState a = scMechanicsState();
    const std::vector<std::uint8_t> comp = scPresetComp(a);
    const DecodedPresetState d = decodePresetState(comp);
    REQUIRE(d.ok);
    REQUIRE(d.morph.position == 0.0f);

    SECTION("AT-10 primary: two arms (E1, C3), E_sub, bar 2 dB, both pass, no override") {
        const std::vector<ShowcaseArm> arms = showcaseColumn(d, comp, ScCap::AT10, true);
        REQUIRE(arms.size() == 2u);
        CHECK(arms[0].note == auRd::kAuE1);
        CHECK(arms[1].note == auRd::kAuC3);
        for (const ShowcaseArm& arm : arms) {
            INFO(auRd::auNoteName(arm.note) << ": lo " << arm.lo << " hi " << arm.hi << " value " << arm.value
                                            << " bar " << arm.barText << " overrides \"" << arm.overrides << "\"");
            CHECK(arm.column == ScCap::AT10);
            CHECK(arm.descriptor == "E_sub");
            CHECK(arm.bar == 2.0);
            CHECK(arm.measured);
            CHECK(arm.pass);
            CHECK(arm.fidelityPass);
            CHECK(arm.overrides.empty());
        }
    }

    SECTION("endpoint guard non-vacuity: a declaration that omits A(P) refuses the arm's own render") {
        const ShowcasePlan plan = showcaseArmPlan(d, ScCap::AT10);
        REQUIRE(plan.overrides.empty());
        // The plan declares its lo pair and A(P) = (P.a, P.b).
        const ::Profundum::LeafBits aBits = ::Profundum::leafBitsOf(a);
        std::vector<std::pair<Krate::DSP::MorphState, Krate::DSP::MorphState>> omitted;
        bool hadA = false;
        for (const auto& pr : plan.declared) {
            if (::Profundum::leafBitsOf(pr.first) == aBits && ::Profundum::leafBitsOf(pr.second) == aBits) {
                hadA = true;
            } else {
                omitted.push_back(pr);
            }
        }
        REQUIRE(hadA);
        REQUIRE(!omitted.empty());

        // The arm's hi render (S(P), a short held E1) under the declaration without A(P).
        constexpr std::uint64_t kTotal = 9600;   // 0.2 s
        const auPt::VoiceEngineScript hiScript =
            auRd::auScript(preset_showcase_detail::settingOf(plan.state), auRd::kAuE1, kTotal);
        PresetArmContext ctx;
        ctx.preset = &plan.state;
        ctx.comp = comp;
        ctx.allowedEndpoints = omitted;
        const auPt::VoiceEngineTaps refused = renderScriptThroughProcessor(hiScript, false, -1, ctx);
        INFO("stop: \"" << ctx.stop << "\"");
        CHECK(ctx.stop.find("hard-codes its base") != std::string::npos);
        CHECK(refused.outL.empty());

        // With the plan's own declaration the same render runs.
        ctx.allowedEndpoints = plan.declared;
        const auPt::VoiceEngineTaps rendered = renderScriptThroughProcessor(hiScript, false, -1, ctx);
        INFO("stop: \"" << ctx.stop << "\"");
        CHECK(ctx.stop.empty());
        CHECK(rendered.outL.size() == kTotal);
    }

    SECTION("primary override: AT-31 (needs SPREAD) claimed by a SOLID preset") {
        Krate::DSP::MorphState solid = a;
        solid.field.mode = Krate::DSP::BassStereoField::ImageMode::Solid;
        solid.field.spread = 1.0f;
        REQUIRE(Krate::DSP::isValidMorphState(solid));
        const std::vector<std::uint8_t> solidComp = scPresetComp(solid);
        const DecodedPresetState ds = decodePresetState(solidComp);
        REQUIRE(ds.ok);

        const std::vector<ShowcaseArm> primary = showcaseColumn(ds, solidComp, ScCap::AT31, true);
        REQUIRE(primary.size() == 2u);
        for (const ShowcaseArm& arm : primary) {
            INFO("primary " << auRd::auNoteName(arm.note) << ": overrides \"" << arm.overrides << "\"");
            CHECK(!arm.overrides.empty());
            CHECK(!arm.pass);
        }

        const std::vector<ShowcaseArm> secondary = showcaseColumn(ds, solidComp, ScCap::AT31, false);
        REQUIRE(secondary.size() == 2u);
        for (const ShowcaseArm& arm : secondary) {
            INFO("secondary " << auRd::auNoteName(arm.note) << ": lo " << arm.lo << " hi " << arm.hi << " value "
                              << arm.value << " overrides \"" << arm.overrides << "\"");
            CHECK(arm.overrides == "kFieldImageModeId=Spread");
            CHECK(arm.measured);
            CHECK(arm.descriptor == "D_st");
            CHECK(presetFloatIsFinite(static_cast<float>(arm.value)));
            CHECK((arm.lo != 0.0 || arm.hi != 0.0));
        }
    }
}

TEST_CASE("Profundum_PresetDistanceMath", "[profundum][distinct]") {
    // Two constructed descriptor pairs (E1, C3). Every component valid, then three masked component-note slots.
    std::array<PresetDescriptor, 2> p{};
    std::array<PresetDescriptor, 2> q{};
    for (std::size_t n = 0; n < 2; ++n) {
        p[n].valid.fill(true);
        q[n].valid.fill(true);
        p[n].x.fill(0.0);
        q[n].x.fill(0.0);
        p[n].x[5] = -10.0;   // side / mid components start at a legal -10 dB on both sides
        q[n].x[5] = -10.0;
    }
    std::size_t masked = 99;
    CHECK(presetDistance(p, q, masked) == 0.0);
    CHECK(masked == 0u);

    // Unmasked differences, one per unit class (hand computation in the comments):
    q[0].x[0] = 3.0;     // band shape, 3 dB unit:            (3 / 3)^2      = 1
    q[0].x[5] = -50.0;   // side / mid clamped to -40:        (30 / 3)^2     = 100
    p[1].x[6] = 5.0;     // side / mid clamped to 0 vs -3:    (3 / 3)^2      = 1
    q[1].x[6] = -3.0;
    q[0].x[11] = 0.5;    // D_br, 0.25 oct unit:              (0.5 / 0.25)^2 = 4
    q[0].x[12] = 4.0;    // E_sub - mid total, 2 dB unit:     (4 / 2)^2      = 4
    q[0].x[13] = 1.0;    // log2 t90, unit 1:                 1^2            = 1
    q[1].x[14] = -6.0;   // E_sub p2p, 3 dB unit:             (6 / 3)^2      = 4
    q[1].x[10] = 9.0;    // D_nl, 3 dB unit:                  (9 / 3)^2      = 9
    // Masked slots carry large differences that must not count: (E1, 2) invalid in p, (C3, 7) invalid in q,
    // (C3, 12) invalid in both (one slot).
    p[0].valid[2] = false;
    q[0].x[2] = 300.0;
    q[1].valid[7] = false;
    q[1].x[7] = -30.0;
    p[1].valid[12] = false;
    q[1].valid[12] = false;
    q[1].x[12] = 500.0;

    const double expected = std::sqrt(1.0 + 100.0 + 1.0 + 4.0 + 4.0 + 1.0 + 4.0 + 9.0);   // sqrt(124)
    masked = 0;
    const double dpq = presetDistance(p, q, masked);
    CHECK(std::abs(dpq - expected) <= 1.0e-12);
    CHECK(masked == 3u);
    std::size_t maskedQp = 0;
    const double dqp = presetDistance(q, p, maskedQp);
    CHECK(std::abs(dqp - expected) <= 1.0e-12);
    CHECK(maskedQp == 3u);

    // F = max(2.5 t_max, 1.0) (P-7).
    CHECK(std::abs(presetDistinctnessThreshold(0.1) - 1.0) <= 1.0e-12);
    CHECK(std::abs(presetDistinctnessThreshold(0.8) - 2.0) <= 1.0e-12);
    CHECK(std::abs(presetDistinctnessThreshold(0.0) - 1.0) <= 1.0e-12);
}

TEST_CASE("Profundum_PresetShowcase_0of8", "[profundum][showcase][long][profundum-showcase]") {
    scRunShowcaseShard(0);
}
TEST_CASE("Profundum_PresetShowcase_1of8", "[profundum][showcase][long][profundum-showcase]") {
    scRunShowcaseShard(1);
}
TEST_CASE("Profundum_PresetShowcase_2of8", "[profundum][showcase][long][profundum-showcase]") {
    scRunShowcaseShard(2);
}
TEST_CASE("Profundum_PresetShowcase_3of8", "[profundum][showcase][long][profundum-showcase]") {
    scRunShowcaseShard(3);
}
TEST_CASE("Profundum_PresetShowcase_4of8", "[profundum][showcase][long][profundum-showcase]") {
    scRunShowcaseShard(4);
}
TEST_CASE("Profundum_PresetShowcase_5of8", "[profundum][showcase][long][profundum-showcase]") {
    scRunShowcaseShard(5);
}
TEST_CASE("Profundum_PresetShowcase_6of8", "[profundum][showcase][long][profundum-showcase]") {
    scRunShowcaseShard(6);
}
TEST_CASE("Profundum_PresetShowcase_7of8", "[profundum][showcase][long][profundum-showcase]") {
    scRunShowcaseShard(7);
}

// =============================================================================
// T023 (FR-013; SC-008; plan 5.4 "Reference reverse arms", 5.6 "SOLID"): Profundum_ReferenceColumns.
// =============================================================================

namespace {

/// The library definition named @p name; REQUIREs it exists.
[[nodiscard]] const ::Profundum::PresetDefs::ProfundumPresetDef& scDefNamed(std::string_view name) {
    const auto& defs = ::Profundum::PresetDefs::allPresets();
    const auto it = std::find_if(defs.begin(), defs.end(), [name](const auto& d) { return d.name == name; });
    INFO("library preset \"" << std::string(name) << "\"");
    REQUIRE(it != defs.end());
    return *it;
}

/// Plan 5.6 "Lanes": the R-SOLID preset's own short render. Comp -> setState -> prepare(48 kHz, 512) -> E1 on at 0
/// (velocity 100 / 127), off at 0.5 s, total 0.75 s (the FR-046 span).
struct ScShortRender {
    std::vector<float> L, R;
};

[[nodiscard]] ScShortRender scShortRender(std::span<const std::uint8_t> comp) {
    constexpr double kFs = 48000.0;
    constexpr std::size_t kBlock = 512;
    const auto noteOff = static_cast<std::size_t>(std::llround(0.5 * kFs));
    const auto total = static_cast<std::size_t>(std::llround(0.75 * kFs));
    REQUIRE(comp.size() == ::Profundum::kStateV3Bytes);

    auto fx = std::make_unique<ProcessorFixture>();
    StateBytes bytes(comp.size());
    std::memcpy(bytes.data(), comp.data(), comp.size());
    REQUIRE(loadStateBytes(*fx->proc, bytes) == Steinberg::kResultOk);
    REQUIRE(fx->prepare(kFs, static_cast<Steinberg::int32>(kBlock)) == Steinberg::kResultOk);

    using Ev = ProcessorFixture::ScriptedEvent;
    const std::vector<Ev> script{
        Ev{.sample = 0,
           .kind = Ev::Kind::NoteOn,
           .pitch = static_cast<Steinberg::int16>(auRd::kAuE1),
           .velocity = static_cast<float>(auRd::kAuVelocity) / 127.0f},
        Ev{.sample = static_cast<Steinberg::int64>(noteOff),
           .kind = Ev::Kind::NoteOff,
           .pitch = static_cast<Steinberg::int16>(auRd::kAuE1)},
    };
    fx->renderScript(script, total, kBlock);
    REQUIRE(fx->proc->endpointRejectionsForTest() == 0u);
    fx->proc->setActive(0u);
    REQUIRE(fx->capturedL.size() == total);
    REQUIRE(fx->capturedR.size() == total);
    return ScShortRender{.L=std::move(fx->capturedL), .R=std::move(fx->capturedR)};
}

}  // namespace

TEST_CASE("Profundum_ReferenceColumns", "[profundum][showcase]") {
    namespace Pd = ::Profundum::PresetDefs;
    using Img = Krate::DSP::BassStereoField::ImageMode;
    using AMode = Krate::DSP::AnchorOscillator::Mode;
    using AWave = Krate::DSP::AnchorOscillator::Waveform;
    enableFTZDAZ();

    struct Ref {
        const char* name;
        ScCap column;
        const char* stored;   // what both endpoints must decode as
        const char* bar;      // the column the reverse arm clears (SC-008)
    };
    const std::array<Ref, 3> refs{{
        {.name="Mono Bedrock", .column=ScCap::RSolid, .stored="SOLID", .bar="AT-29"},
        {.name="Locked Floor", .column=ScCap::RLock, .stored="LOCK", .bar="AT-11"},
        {.name="Bedrock Sine", .column=ScCap::RSine, .stored="Sine", .bar="AT-14"},
    }};

    for (const Ref& ref : refs) {
        const Pd::ProfundumPresetDef& def = scDefNamed(ref.name);
        INFO("preset " << ref.name << " (" << ref.stored << ", reverse arm against the " << ref.bar << " bar)");
        CHECK(def.primary == ref.column);
        std::vector<std::uint8_t> comp;
        std::string why;
        const bool built = buildPresetComponentState(def, comp, why);
        INFO("why: " << why);
        REQUIRE(built);
        const DecodedPresetState d = decodePresetState(comp);
        REQUIRE(d.ok);

        // The decoded state stores the reference value on BOTH endpoints.
        switch (ref.column) {
            case ScCap::RSolid:
                CHECK(d.a.field.mode == Img::Solid);
                CHECK(d.b.field.mode == Img::Solid);
                break;
            case ScCap::RLock:
                CHECK(d.a.anchor.mode == AMode::Lock);
                CHECK(d.b.anchor.mode == AMode::Lock);
                break;
            default:
                CHECK(d.a.anchor.waveform == AWave::Sine);
                CHECK(d.b.anchor.waveform == AWave::Sine);
                break;
        }

        // The plan 5.4 reverse arm at E1 and C3, as a primary: measured, passing, fidelity-clean, no override.
        const std::vector<ShowcaseArm> arms = showcaseColumn(d, comp, ref.column, true);
        REQUIRE(arms.size() == 2u);
        CHECK(arms[0].note == auRd::kAuE1);
        CHECK(arms[1].note == auRd::kAuC3);
        for (const ShowcaseArm& arm : arms) {
            INFO(auRd::auNoteName(arm.note) << ": " << arm.descriptor << " lo " << arm.lo << " hi " << arm.hi
                                            << " value " << arm.value << " bar " << arm.barText << " overrides \""
                                            << arm.overrides << "\"");
            std::cout << "# Profundum_ReferenceColumns: " << ref.name << ',' << auRd::auNoteName(arm.note) << ','
                      << arm.descriptor << ",lo=" << arm.lo << ",hi=" << arm.hi << ",value=" << arm.value
                      << ",bar=" << arm.bar << ",pass=" << (arm.pass ? 1 : 0) << '\n';
            CHECK(arm.column == ref.column);
            CHECK(arm.measured);
            CHECK(arm.overrides.empty());
            CHECK(arm.pass);
            CHECK(arm.fidelityPass);
        }

        // R-SOLID (plan 5.6 "SOLID"): its own short render reads L == R bitwise from s0 = 544 on.
        if (ref.column == ScCap::RSolid) {
            REQUIRE(presetSolidActive(d));
            const std::size_t s0 = presetSolidSettleSample(48000.0);
            CHECK(s0 == 544u);
            const ScShortRender r = scShortRender(comp);
            std::size_t mismatches = 0;
            std::size_t firstMismatch = r.L.size();
            float peak = 0.0f;
            for (std::size_t i = s0; i < r.L.size(); ++i) {
                if (std::bit_cast<std::uint32_t>(r.L[i]) != std::bit_cast<std::uint32_t>(r.R[i])) {
                    if (mismatches == 0) {
                        firstMismatch = i;
                    }
                    ++mismatches;
                }
                peak = std::max(peak, std::abs(r.L[i]));
            }
            INFO("L != R at " << mismatches << " samples from s0 = " << s0 << " (first " << firstMismatch
                              << "); peak " << peak);
            CHECK(mismatches == 0u);
            CHECK(peak > 1.0e-3f);   // non-vacuity: the compared span carries the note
            CHECK(presetFloatIsFinite(peak));
        }
    }
}
