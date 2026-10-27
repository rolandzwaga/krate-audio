// Profundum Phase 13 preset library tests (specs/profundum-phase13-presets-release/tasks.md)
//
// Placeholder created by T007 (Deviation 1). Cases written by T009, T010, T023, T024:
// FR-001 categories, PresetHost / generator (FR-024, FR-027, FR-047, FR-049), Profundum_PresetDefsRules (SC-006 rules),
// library checks SC-001 - SC-006, SC-010, SC-012, SC-014, SC-030.

#include <catch2/catch_test_macros.hpp>
#include "profundum_test_fixture.h"

#include "preset/preset_manager.h"   // T024: isValidPresetName (SC-003)
#include "preset/profundum_preset_config.h"
#include "profundum_preset_defs.h"
#include "profundum_preset_host.h"
#include "preset_test_support.h"
#include "preset_render_support.h"   // T023: showcaseArmPlan (plan 6 rules 7, 8)
#include "parameters/morph_params.h"
#include "parameters/sound_param_row.h"

#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <cctype>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <process.h>   // _getpid: the GeneratorSmoke temp-dir name (T010)
#else
#include <unistd.h>    // getpid
#endif

namespace {

std::string presetAsciiOf(const Steinberg::Vst::TChar* s) {
    char buf[256] = {};
    Steinberg::UString(const_cast<Steinberg::Vst::TChar*>(s), 128)  // NOLINT(cppcoreguidelines-pro-type-const-cast)
        .toAscii(buf, 256);
    return {buf};
}

std::string presetLower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

/// C-1 forbidden vocabulary, every entry lower-cased.
// NOLINTNEXTLINE(bugprone-exception-escape) -- implicit special members of std::set/std::string members; test-only value type
struct CategoryVocabulary {
    std::set<std::string> fullNames;     // titles, list strings, group names
    std::set<std::string> leadingWords;  // the first word of every title
};

CategoryVocabulary buildCategoryVocabulary() {
    CategoryVocabulary vocab;
    auto fixture = std::make_unique<ProfundumPluginTest::ControllerFixture>();
    auto& ctl = *fixture->controller;

    const Steinberg::int32 count = ctl.getParameterCount();
    REQUIRE(count == 95);
    for (Steinberg::int32 i = 0; i < count; ++i) {
        Steinberg::Vst::ParameterInfo info{};
        REQUIRE(ctl.getParameterInfo(i, info) == Steinberg::kResultOk);
        const std::string title = presetAsciiOf(info.title);
        REQUIRE_FALSE(title.empty());
        vocab.fullNames.insert(presetLower(title));
        vocab.leadingWords.insert(presetLower(title.substr(0, title.find(' '))));

        if (info.stepCount > 0) {
            for (Steinberg::int32 k = 0; k <= info.stepCount; ++k) {
                const double v = static_cast<double>(k) / static_cast<double>(info.stepCount);
                Steinberg::Vst::String128 s{};
                REQUIRE(ctl.getParamStringByValue(info.id, v, s) == Steinberg::kResultOk);
                vocab.fullNames.insert(presetLower(presetAsciiOf(s)));
            }
        }
    }

    for (const char* group : {"Core", "Character", "Anchor", "Pitch", "Envelope", "Mass", "Field", "Motion", "Morph",
                              "NoteMemory", "Macros"}) {
        vocab.fullNames.insert(presetLower(group));
    }
    return vocab;
}

/// C-1 / FR-001: a category name fails when it equals (case-insensitive) a title, a
/// list string, a group name, or - unless it is a continuum word - a title's leading word.
bool categoryNamePasses(const CategoryVocabulary& vocab, std::string_view name) {
    const std::string lower = presetLower(name);
    if (vocab.fullNames.contains(lower)) {
        return false;
    }
    static const std::set<std::string> kContinuumWords{"sub", "resonant", "harmonic", "distorted"};
    return kContinuumWords.contains(lower) || !vocab.leadingWords.contains(lower);
}

}  // namespace

// T009 (FR-001; C-1; plan 4.2).
TEST_CASE("Profundum_PresetCategoryConfig", "[profundum][preset]") {
    const std::vector<std::string> expected{"Sub", "Resonant", "Harmonic", "Driven", "Plucked", "Animated", "Kicks"};
    const Krate::Plugins::PresetManagerConfig config = ::Profundum::makeProfundumPresetConfig();

    REQUIRE(config.subcategoryNames.size() == 7);   // Phase 14b: Kicks
    CHECK(config.subcategoryNames[0] == "Sub");
    CHECK(config.subcategoryNames == expected);
    CHECK(config.pluginName == "Profundum");
    CHECK(config.pluginCategoryDesc == "Synth");

    const std::vector<std::string> expectedTabs{"All",     "Sub",      "Resonant", "Harmonic",
                                                "Driven",  "Plucked",  "Animated", "Kicks"};
    CHECK(::Profundum::makeProfundumPresetTabLabels() == expectedTabs);

    REQUIRE(::Profundum::PresetDefs::kCategories.size() == config.subcategoryNames.size());
    for (std::size_t i = 0; i < config.subcategoryNames.size(); ++i) {
        INFO("category " << i);
        CHECK(config.subcategoryNames[i] == std::string(::Profundum::PresetDefs::kCategories[i]));
    }
}

TEST_CASE("Profundum_PresetCategoryNameRule", "[profundum][preset]") {
    const CategoryVocabulary vocab = buildCategoryVocabulary();

    // Non-vacuity: the predicate rejects vocabulary words, and the continuum exception holds.
    REQUIRE(vocab.leadingWords.contains("harmonic"));  // "Harmonic Fold"
    for (const char* bad : {"Body", "Wide", "Drift", "Mass", "Drop"}) {
        INFO("forbidden " << bad);
        CHECK_FALSE(categoryNamePasses(vocab, bad));
    }
    CHECK(categoryNamePasses(vocab, "Harmonic"));

    for (const std::string& name : ::Profundum::makeProfundumPresetConfig().subcategoryNames) {
        INFO("category " << name);
        CHECK(categoryNamePasses(vocab, name));
    }
}

// =============================================================================
// T010 (FR-024, FR-027, FR-047, FR-049; C-4; plan 4.4, 4.5). Every definition below is TEST-LOCAL: built in this
// TU, never added to PresetDefs::allPresets().
// =============================================================================

namespace {

namespace pd = ::Profundum::PresetDefs;

[[nodiscard]] long presetTestPid() {
#ifdef _WIN32
    return static_cast<long>(_getpid());
#else
    return static_cast<long>(getpid());
#endif
}

/// T1: endpoint A {Mass 0.80, Character Amount 0.5}, endpoint B {Mass 0.20}, controls {Morph Position 0, Weight 0.6},
/// names "Deep Floor" / "Bright Edge", seed 0x00001234.
[[nodiscard]] pd::ProfundumPresetDef makeHostDefT1() {
    pd::ProfundumPresetDef d{};
    d.name = "Host Test T1";
    d.category = "Sub";
    d.description = "test-local definition";
    d.primary = pd::Capability::AT01;
    d.endpointA = {{.id=::Profundum::kMassId, .normalized=0.80}, {.id=::Profundum::kCharacterAmountId, .normalized=0.5}};
    d.endpointB = {{.id=::Profundum::kMassId, .normalized=0.20}};
    d.controls = {{.id=::Profundum::kMorphPositionId, .normalized=0.0}, {.id=::Profundum::kMacroWeightId, .normalized=0.6}};
    d.nameA = "Deep Floor";
    d.nameB = "Bright Edge";
    d.seed = 0x00001234u;
    return d;
}

[[nodiscard]] double massPlainOf(double normalized) {
    return ::Profundum::plainFromNormalized(::Profundum::lawOf(::Profundum::kMassLeaf), normalized);
}

/// Removes a directory tree on construction and on scope exit (both smoke-test temp dirs).
struct TempDirGuard {
    std::filesystem::path path;
    explicit TempDirGuard(std::filesystem::path p) : path(std::move(p)) {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    ~TempDirGuard() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    TempDirGuard(const TempDirGuard&) = delete;
    TempDirGuard& operator=(const TempDirGuard&) = delete;
};

/// Every regular file under @p root, keyed by its generic relative path, with its bytes.
[[nodiscard]] std::map<std::string, std::vector<char>> fileSetOf(const std::filesystem::path& root) {
    std::map<std::string, std::vector<char>> out;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        std::ifstream f(entry.path(), std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        out[std::filesystem::relative(entry.path(), root).generic_string()] = std::move(bytes);
    }
    return out;
}

[[nodiscard]] std::uint32_t seedWordOf(const std::vector<std::uint8_t>& comp) {
    return static_cast<std::uint32_t>(comp[84]) | (static_cast<std::uint32_t>(comp[85]) << 8) |
           (static_cast<std::uint32_t>(comp[86]) << 16) | (static_cast<std::uint32_t>(comp[87]) << 24);
}

}  // namespace

TEST_CASE("Profundum_PresetHostDrive", "[profundum][preset]") {
    using ProfundumPluginTest::buildPresetComponentState;
    using ProfundumPluginTest::decodePresetState;
    using ProfundumPluginTest::DecodedPresetState;

    const pd::ProfundumPresetDef t1 = makeHostDefT1();
    std::vector<std::uint8_t> comp;
    std::string why;
    const bool built = buildPresetComponentState(t1, comp, why);
    INFO("why: " << why);
    REQUIRE(built);

    const DecodedPresetState d = decodePresetState(comp);
    REQUIRE(d.ok);
    CHECK(d.version == 3);
    CHECK(d.bytes == 626);   // Phase 15 (Phase 14: 610)
    CHECK(d.morph.editTarget == 0);
    // Seed step non-vacuity: without the step-6 patch the seed would read 0xB455.
    CHECK(d.seed == 0x1234u);
    CHECK(d.names[0] == "Deep Floor");
    CHECK(d.names[1] == "Bright Edge");
    REQUIRE(std::abs(massPlainOf(0.80) - massPlainOf(0.20)) > 2.0 * 2.5e-4 * 100.0);   // the two arms are distinct
    CHECK(std::abs(static_cast<double>(d.a.mass) - massPlainOf(0.80)) <= 2.5e-4 * 100.0);
    CHECK(std::abs(static_cast<double>(d.b.mass) - massPlainOf(0.20)) <= 2.5e-4 * 100.0);
    CHECK(std::abs(static_cast<double>(d.macros[0]) - 0.6) <= 2.5e-4);
    // FR-047: the runtime blob's target and ramped position are the stored Morph Position (0).
    CHECK(std::abs(static_cast<double>(d.morph.position)) <= 2.5e-4);
    CHECK(std::abs(static_cast<double>(d.runtime.target) - static_cast<double>(d.morph.position)) <= 2.5e-4);
    CHECK(std::abs(static_cast<double>(d.runtime.position) - static_cast<double>(d.morph.position)) <= 2.5e-4);

    // T2: an empty endpointB copies endpoint A leaf for leaf.
    {
        pd::ProfundumPresetDef t2 = makeHostDefT1();
        t2.endpointB.clear();
        std::vector<std::uint8_t> comp2;
        std::string why2;
        const bool built2 = buildPresetComponentState(t2, comp2, why2);
        INFO("T2 why: " << why2);
        REQUIRE(built2);
        const DecodedPresetState d2 = decodePresetState(comp2);
        REQUIRE(d2.ok);
        const ::Profundum::LeafBits aBits = ::Profundum::leafBitsOf(d2.a);
        const ::Profundum::LeafBits bBits = ::Profundum::leafBitsOf(d2.b);
        for (std::size_t i = 0; i < ::Profundum::kNumLeaves; ++i) {
            INFO("leaf " << i);
            CHECK(aBits[i] == bBits[i]);
        }
        CHECK(std::abs(static_cast<double>(d2.b.mass) - massPlainOf(0.80)) <= 2.5e-4 * 100.0);
    }

    // T3: no seed (the struct default) stores the engine's default seed.
    {
        pd::ProfundumPresetDef t3 = makeHostDefT1();
        t3.seed = pd::ProfundumPresetDef{}.seed;
        REQUIRE(t3.seed == pd::kFactoryDefaultSeed);
        std::vector<std::uint8_t> comp3;
        std::string why3;
        const bool built3 = buildPresetComponentState(t3, comp3, why3);
        INFO("T3 why: " << why3);
        REQUIRE(built3);
        REQUIRE(comp3.size() >= 88);
        CHECK(seedWordOf(comp3) == Krate::DSP::ProfundumEngine::kDefaultSeed);
        CHECK(seedWordOf(comp3) == 0xB455u);
        CHECK(decodePresetState(comp3).seed == 0xB455u);
    }
}

TEST_CASE("Profundum_PresetHostValidation", "[profundum][preset]") {
    struct Case {
        std::string label;
        pd::ProfundumPresetDef def;
        std::string point;   // what `why` must name besides the preset
    };
    std::vector<Case> cases;
    const auto add = [&cases](std::string label, std::string point, const auto& mutate) {
        pd::ProfundumPresetDef d = makeHostDefT1();
        d.name = "Validation Probe";
        mutate(d);
        cases.push_back(Case{.label=std::move(label), .def=std::move(d), .point=std::move(point)});
    };

    volatile std::uint32_t nanBits = 0x7FC00000u;
    const double nanValue = static_cast<double>(std::bit_cast<float>(static_cast<std::uint32_t>(nanBits)));
    REQUIRE_FALSE(ProfundumPluginTest::presetFloatIsFinite(static_cast<float>(nanValue)));

    static constexpr std::string_view kLongName = "abcdefghijklmnopqrstuvwxyz012345";   // 32 bytes
    static constexpr std::string_view kDirtyName = "Bright\x01" "Edge";                 // a control byte
    REQUIRE(kLongName.size() == 32);
    REQUIRE(::Profundum::sanitizeEndpointName(kDirtyName).view() != kDirtyName);

    add("NaN value", "endpointA[0]", [nanValue](pd::ProfundumPresetDef& d) { d.endpointA[0].normalized = nanValue; });
    add("value 1.5", "endpointA[0]", [](pd::ProfundumPresetDef& d) { d.endpointA[0].normalized = 1.5; });
    add("Global ID 3 in controls", "(ID 3)",
        [](pd::ProfundumPresetDef& d) { d.controls.push_back({.id=::Profundum::kOutputCeilingId, .normalized=0.5}); });
    add("Sound ID in controls", "(ID 104)",
        [](pd::ProfundumPresetDef& d) { d.controls.push_back({.id=::Profundum::kMassId, .normalized=0.5}); });
    for (const Steinberg::Vst::ParamID id : {0u, 2u, 3u, 417u, 4u, 5u, 6u, 7u, 1001u}) {
        add("ID " + std::to_string(id) + " in controls", "controls[2] (ID " + std::to_string(id) + ")",
            [id](pd::ProfundumPresetDef& d) { d.controls.push_back({.id=id, .normalized=0.0}); });
    }
    add("non-Sound ID in endpointA", "endpointA[2] (ID 105)",
        [](pd::ProfundumPresetDef& d) { d.endpointA.push_back({.id=::Profundum::kMorphPositionId, .normalized=0.0}); });
    add("duplicated ID", "duplicates",
        [](pd::ProfundumPresetDef& d) { d.endpointB.push_back({.id=::Profundum::kMassId, .normalized=0.3}); });
    add("name A \"A\"", "nameA", [](pd::ProfundumPresetDef& d) { d.nameA = "A"; });
    add("name B \"B\"", "nameB", [](pd::ProfundumPresetDef& d) { d.nameB = "B"; });
    add("32-byte name", "nameA", [](pd::ProfundumPresetDef& d) { d.nameA = kLongName; });
    add("name sanitizeEndpointName changes", "nameB", [](pd::ProfundumPresetDef& d) { d.nameB = kDirtyName; });

    // Non-vacuity: the unmodified base passes.
    {
        pd::ProfundumPresetDef base = makeHostDefT1();
        base.name = "Validation Probe";
        std::vector<std::uint8_t> comp;
        std::string why;
        const bool built = ProfundumPluginTest::buildPresetComponentState(base, comp, why);
        INFO("base why: " << why);
        CHECK(built);
    }

    for (const Case& c : cases) {
        std::vector<std::uint8_t> comp;
        std::string why;
        const bool built = ProfundumPluginTest::buildPresetComponentState(c.def, comp, why);
        INFO("case: " << c.label << "; why: " << why);
        CHECK_FALSE(built);
        CHECK(why.find("Validation Probe") != std::string::npos);
        CHECK(why.find(c.point) != std::string::npos);
        CHECK(comp.empty());
    }
}

TEST_CASE("Profundum_GeneratorSmoke", "[profundum][preset]") {
    const std::filesystem::path base = std::filesystem::temp_directory_path();
    const std::string stem = "profundum-gen-smoke-" + std::to_string(presetTestPid());
    const TempDirGuard first(base / stem);
    const TempDirGuard second(base / (stem + "-b"));

    std::size_t written = 0;
    std::string why;
    const bool ok = ProfundumPluginTest::writeFactoryTree(first.path, written, why);
    INFO("why: " << why);
    REQUIRE(ok);
    CHECK(written == pd::allPresets().size());
    CHECK(written == 73u);   // T024 (SC-010): the whole library; Phase 14 T023 adds two (59-60), Phase 14b eleven kicks,
                             // Phase 15 two (72-73)
    for (const std::string_view category : pd::kCategories) {
        INFO("category " << category);
        CHECK(std::filesystem::is_directory(first.path / std::string(category)));
    }

    std::size_t written2 = 0;
    std::string why2;
    const bool ok2 = ProfundumPluginTest::writeFactoryTree(second.path, written2, why2);
    INFO("second why: " << why2);
    REQUIRE(ok2);
    CHECK(written2 == written);

    const auto setA = fileSetOf(first.path);
    const auto setB = fileSetOf(second.path);
    CHECK(setA.size() == written);
    REQUIRE(setA.size() == setB.size());
    for (const auto& [rel, bytes] : setA) {
        INFO("file " << rel);
        const auto it = setB.find(rel);
        REQUIRE(it != setB.end());
        REQUIRE(it->second.size() == bytes.size());
        CHECK((bytes.empty() || std::memcmp(bytes.data(), it->second.data(), bytes.size()) == 0));
    }
}

// =============================================================================
// T023 (SC-006 rules; plan 6 rules 1-10; C-6, C-9, P-2 - P-6, FR-047, FR-049): Profundum_PresetDefsRules.
// Every rule is a predicate over (definition, decoded Comp); "" means it holds, otherwise the text names the point.
// =============================================================================

namespace {

using DrMS = Krate::DSP::MorphState;
using DrImg = Krate::DSP::BassStereoField::ImageMode;
using DrAlg = Krate::DSP::BassCharacterStage::Algorithm;
using DrSrc = Krate::DSP::MorphState::MotionConfig::Source;
using DrPol = Krate::DSP::MorphState::MotionConfig::Polarity;
using DrFieldSrc = Krate::DSP::BassStereoField::MotionSource;
using DrRule = std::string (*)(const pd::ProfundumPresetDef&, const ProfundumPluginTest::DecodedPresetState&);

[[nodiscard]] std::string drNum(double v) {
    return ProfundumPluginTest::preset_decode_detail::fmt(v);
}

/// Plan 5.6 active endpoints: A alone at p = 0, B alone at p = 1, both for 0 < p < 1 (a non-finite p: both).
[[nodiscard]] std::vector<const DrMS*> drActiveEndpoints(const ProfundumPluginTest::DecodedPresetState& d) {
    std::vector<const DrMS*> out;
    const float p = d.morph.position;
    if (!(p >= 1.0f)) {
        out.push_back(&d.a);
    }
    if (!(p <= 0.0f)) {
        out.push_back(&d.b);
    }
    return out;
}

/// Every claim of a definition: the primary first, then the secondaries.
[[nodiscard]] std::vector<pd::Capability> drClaims(const pd::ProfundumPresetDef& def) {
    std::vector<pd::Capability> c{def.primary};
    c.insert(c.end(), def.secondaries.begin(), def.secondaries.end());
    return c;
}

[[nodiscard]] std::string drLabel(pd::Capability c) {
    const auto i = static_cast<std::size_t>(c);
    return i < pd::kNumCapabilities ? std::string(pd::capabilityTable()[i].label) : std::string("Capability::Count");
}

/// Both endpoints, named.
[[nodiscard]] std::array<std::pair<const char*, const DrMS*>, 2> drBothEnds(
    const ProfundumPluginTest::DecodedPresetState& d) {
    return {{{"A", &d.a}, {"B", &d.b}}};
}

// Rule 1 (C-6): on both endpoints amp attack <= 1000 ms, attack + decay <= 2000 ms, release <= 4000 ms; Morph ramp
// <= 2000 ms.
std::string drRule1(const pd::ProfundumPresetDef& /*def*/, const ProfundumPluginTest::DecodedPresetState& d) {
    constexpr double kSlack = 1.0e-3;   // ms: the float of a log-law value sits within this of its plain bound
    for (const auto& [name, s] : drBothEnds(d)) {
        const double atk = static_cast<double>(s->ampEnv.attackMs);
        const double dec = static_cast<double>(s->ampEnv.decayMs);
        const double rel = static_cast<double>(s->ampEnv.releaseMs);
        if (!(atk <= 1000.0 + kSlack)) {
            return std::string("endpoint ") + name + " attack " + drNum(atk) + " ms > 1000 ms";
        }
        if (!(atk + dec <= 2000.0 + kSlack)) {
            return std::string("endpoint ") + name + " attack + decay " + drNum(atk + dec) + " ms > 2000 ms";
        }
        if (!(rel <= 4000.0 + kSlack)) {
            return std::string("endpoint ") + name + " release " + drNum(rel) + " ms > 4000 ms";
        }
    }
    if (!(static_cast<double>(d.morph.rampMs) <= 2000.0 + kSlack)) {
        return "Morph ramp " + drNum(static_cast<double>(d.morph.rampMs)) + " ms > 2000 ms";
    }
    return {};
}

// Rule 2 (P-6, FR-060, FR-047): Output Gain normalized 1.0, Ceiling -1 dBTP, Bend Range 2 st, Polyphony 4, Edit
// Target A.
std::string drRule2(const pd::ProfundumPresetDef& /*def*/, const ProfundumPluginTest::DecodedPresetState& d) {
    if (!(std::abs(static_cast<double>(d.outputGain) - 1.0) <= 1.0e-6)) {
        return "Output Gain " + drNum(static_cast<double>(d.outputGain)) + " != 1.0";
    }
    if (!(std::abs(static_cast<double>(d.outputCeilingDb) + 1.0) <= 1.0e-6)) {
        return "Ceiling " + drNum(static_cast<double>(d.outputCeilingDb)) + " dBTP != -1";
    }
    if (!(std::abs(static_cast<double>(d.note.bendSt) - 2.0) <= 1.0e-6)) {
        return "Bend Range " + drNum(static_cast<double>(d.note.bendSt)) + " st != 2";
    }
    if (d.polyphony != 4) {
        return "Polyphony " + std::to_string(d.polyphony) + " != 4";
    }
    if (d.morph.editTarget != 0) {
        return "Edit Target " + std::to_string(d.morph.editTarget) + " != A (0)";
    }
    return {};
}

// Rule 3 (C-9): an ENV Motion source implies Unipolar, unless the Comment says "resting offset".
std::string drRule3(const pd::ProfundumPresetDef& def, const ProfundumPluginTest::DecodedPresetState& d) {
    if (presetLower(def.description).find("resting offset") != std::string::npos) {
        return {};
    }
    for (const auto& [name, s] : drBothEnds(d)) {
        if (s->motion.source == DrSrc::Env && s->motion.polarity != DrPol::Unipolar) {
            return std::string("endpoint ") + name + " stores ENV Motion with Bipolar polarity";
        }
    }
    return {};
}

// Rule 4 (C-9): never Rectify with Character Tone 1, Fold 100 and WIDE together.
std::string drRule4(const pd::ProfundumPresetDef& /*def*/, const ProfundumPluginTest::DecodedPresetState& d) {
    for (const auto& [name, s] : drBothEnds(d)) {
        if (s->characterAlgorithm == DrAlg::Rectify && s->characterTone >= 1.0f - 1.0e-6f &&
            s->foldAmount >= 100.0f - 1.0e-4f && s->field.mode == DrImg::Wide) {
            return std::string("endpoint ") + name + " stores Rectify + Tone 1 + Fold 100 + WIDE";
        }
    }
    return {};
}

/// Rule 6 on one name: present, <= 31 bytes, sanitised, not the default.
[[nodiscard]] std::string drNameProblem(const char* which, std::string_view name, std::string_view dflt) {
    if (name.empty()) {
        return std::string(which) + " is empty";
    }
    if (name.size() > 31u) {
        return std::string(which) + " \"" + std::string(name) + "\" is " + std::to_string(name.size()) + " bytes";
    }
    if (::Profundum::sanitizeEndpointName(name).view() != name) {
        return std::string(which) + " \"" + std::string(name) + "\" is not sanitised";
    }
    if (name == dflt) {
        return std::string(which) + " is the default \"" + std::string(dflt) + "\"";
    }
    return {};
}

// Rule 6: names A and B present, <= 31 bytes, sanitised, not "A" / "B" (on the definition and on the decoded Comp).
std::string drRule6(const pd::ProfundumPresetDef& def, const ProfundumPluginTest::DecodedPresetState& d) {
    const std::array<std::string, 4> problems{
        drNameProblem("nameA", def.nameA, "A"), drNameProblem("nameB", def.nameB, "B"),
        drNameProblem("decoded name A", d.names[0], "A"), drNameProblem("decoded name B", d.names[1], "B")};
    for (const std::string& p : problems) {
        if (!p.empty()) {
            return p;
        }
    }
    return {};
}

// Rule 7 (P-3, P-4): a single-state claim stores Morph Position 0 (the column's showcase plan records no
// "kMorphPositionId=0" override, plan 5.4); an AT-33 claim stores SPREAD, spread 1 and Field Motion depth 1 on A.
std::string drRule7(const pd::ProfundumPresetDef& def, const ProfundumPluginTest::DecodedPresetState& d) {
    for (const pd::Capability c : drClaims(def)) {
        if (c == pd::Capability::Count) {
            continue;   // rule 10's point
        }
        const ProfundumPluginTest::ShowcasePlan plan = ProfundumPluginTest::showcaseArmPlan(d, c);
        if (std::find(plan.overrides.begin(), plan.overrides.end(), "kMorphPositionId=0") != plan.overrides.end()) {
            return drLabel(c) + " is single-state but the stored Morph Position is " +
                   drNum(static_cast<double>(d.morph.position));
        }
        if (c == pd::Capability::AT33 &&
            (d.a.field.mode != DrImg::Spread || d.a.field.spread != 1.0f || d.a.field.motionDepth != 1.0f)) {
            return "AT-33 claim without SPREAD / spread 1 / Field Motion depth 1 on endpoint A (spread " +
                   drNum(static_cast<double>(d.a.field.spread)) + ", depth " +
                   drNum(static_cast<double>(d.a.field.motionDepth)) + ")";
        }
    }
    return {};
}

// Rule 8 (plan 5.4 column 5): the primary stores every precondition its row names (its showcase plan records no
// override, and the column applies to the stored state: P-2 for AT-13).
std::string drRule8(const pd::ProfundumPresetDef& def, const ProfundumPluginTest::DecodedPresetState& d) {
    if (def.primary == pd::Capability::Count) {
        return {};   // rule 10's point
    }
    const ProfundumPluginTest::ShowcasePlan plan = ProfundumPluginTest::showcaseArmPlan(d, def.primary);
    if (!plan.applicable) {
        return drLabel(def.primary) + " does not apply to the stored state (variant \"" + plan.variant + "\")";
    }
    if (!plan.overrides.empty()) {
        std::string o;
        for (const std::string& s : plan.overrides) {
            if (!o.empty()) {
                o += ';';
            }
            o += s;
        }
        return drLabel(def.primary) + " primary needs precondition overrides: " + o;
    }
    return {};
}

// Rule 9 (C-7): seedDependent is true when an active endpoint stores Motion DRIFT, Field Motion DRIFT with depth > 0,
// or a random Cycle Shape (S&H, Smooth Random; motion_params.h:42) on a CYCLE source.
std::string drRule9(const pd::ProfundumPresetDef& def, const ProfundumPluginTest::DecodedPresetState& d) {
    if (def.seedDependent) {
        return {};
    }
    for (const DrMS* s : drActiveEndpoints(d)) {
        const char* name = s == &d.a ? "A" : "B";
        if (s->motion.source == DrSrc::Drift) {
            return std::string("endpoint ") + name + " stores Motion DRIFT but seedDependent is false";
        }
        if (s->field.motionSource == DrFieldSrc::Drift && s->field.motionDepth > 0.0f) {
            return std::string("endpoint ") + name + " stores Field Motion DRIFT at depth " +
                   drNum(static_cast<double>(s->field.motionDepth)) + " but seedDependent is false";
        }
        const bool randomShape = s->motion.cycleShape == Krate::DSP::Waveform::SampleHold ||
                                 s->motion.cycleShape == Krate::DSP::Waveform::SmoothRandom;
        if (s->motion.source == DrSrc::Cycle && randomShape) {
            return std::string("endpoint ") + name + " stores a random Cycle Shape but seedDependent is false";
        }
    }
    return {};
}

// Rule 10 (per definition): exactly one primary, a real column, not repeated among the secondaries; the secondaries
// are real columns, each claimed once.
std::string drRule10(const pd::ProfundumPresetDef& def, const ProfundumPluginTest::DecodedPresetState& /*d*/) {
    if (def.primary == pd::Capability::Count) {
        return "no primary column";
    }
    std::set<pd::Capability> seen{def.primary};
    for (const pd::Capability c : def.secondaries) {
        if (c == pd::Capability::Count) {
            return "a secondary is Capability::Count";
        }
        if (!seen.insert(c).second) {
            return drLabel(c) + " is claimed twice (a second primary or a repeated secondary)";
        }
    }
    return {};
}

struct DrNamedRule {
    int number;
    DrRule fn;
};

[[nodiscard]] const std::array<DrNamedRule, 9>& drRules() {
    static const std::array<DrNamedRule, 9> kRules{{{.number=1, .fn=&drRule1},
                                                    {.number=2, .fn=&drRule2},
                                                    {.number=3, .fn=&drRule3},
                                                    {.number=4, .fn=&drRule4},
                                                    {.number=6, .fn=&drRule6},
                                                    {.number=7, .fn=&drRule7},
                                                    {.number=8, .fn=&drRule8},
                                                    {.number=9, .fn=&drRule9},
                                                    {.number=10, .fn=&drRule10}}};
    return kRules;
}

[[nodiscard]] DrRule drRuleOf(int number) {
    for (const DrNamedRule& r : drRules()) {
        if (r.number == number) {
            return r.fn;
        }
    }
    return nullptr;
}

/// The test-local base of the non-vacuity defs: primary AT-10 on the registered defaults (Sine, LOCK), p = 0.
[[nodiscard]] pd::ProfundumPresetDef drBaseDef() {
    pd::ProfundumPresetDef d{};
    d.name = "Rules Probe";
    d.category = "Sub";
    d.description = "test-local definition, Anchor Level";
    d.primary = pd::Capability::AT10;
    d.endpointA = {{.id=::Profundum::kAnchorLevelId, .normalized=1.0}};
    d.controls = {{.id=::Profundum::kMorphPositionId, .normalized=0.0}};
    d.nameA = "Rules Floor";
    d.nameB = "Rules Edge";
    return d;
}

[[nodiscard]] ProfundumPluginTest::DecodedPresetState drDecodeOf(const pd::ProfundumPresetDef& def) {
    std::vector<std::uint8_t> comp;
    std::string why;
    const bool built = ProfundumPluginTest::buildPresetComponentState(def, comp, why);
    INFO("preset " << def.name << ": " << why);
    REQUIRE(built);
    const ProfundumPluginTest::DecodedPresetState d = ProfundumPluginTest::decodePresetState(comp);
    REQUIRE(d.ok);
    return d;
}

struct DrViolation {
    int rule;
    std::string label;
    pd::ProfundumPresetDef def;
    ProfundumPluginTest::DecodedPresetState decoded;
};

// -----------------------------------------------------------------------------
// Phase 14 T023 (FR-042, FR-063, FR-046 / P-9; SC-018): the Morph Mod library rules
// -----------------------------------------------------------------------------

namespace mme = ProfundumPluginTest::morph_mod_evidence;
using MmMod = ProfundumPluginTest::DecodedPresetState::MorphMod;

// FR-042: the stored C-10 block holds every field inside its C-12 range; every float finite and inside its law box.
std::string mmRuleLawBox(const ProfundumPluginTest::DecodedPresetState& d) {
    const MmMod& m = d.morphMod;
    if (!m.present) {
        return "no Morph Mod block in the Comp";
    }
    if (m.source < 0 || m.source >= ::Profundum::kNumMorphModSources) {
        return "Morph Mod Source " + std::to_string(m.source) + " outside [0, 4]";
    }
    if (m.cycleShape < 0 || m.cycleShape >= ::Profundum::kNumMorphCycleShapes) {
        return "Morph Cycle Shape " + std::to_string(m.cycleShape) + " outside [0, 5]";
    }
    if (m.cycleSync != 0 && m.cycleSync != 1) {
        return "Morph Cycle Sync " + std::to_string(m.cycleSync) + " not in {0, 1}";
    }
    static_assert(Krate::DSP::kNoteValueDropdownCount == 30, "FR-042: Sync Rate index in [0, 29]");
    if (m.cycleSyncRate < 0 || m.cycleSyncRate >= Krate::DSP::kNoteValueDropdownCount) {
        return "Morph Cycle Sync Rate " + std::to_string(m.cycleSyncRate) + " outside [0, 29]";
    }
    struct MmField {
        const char* name;
        float value;
        const ::Profundum::PlainLaw* law;
    };
    namespace pf = ::Profundum;
    const std::array<MmField, 5> fields{
        {{.name = "Morph Mod Amount", .value = m.amount, .law = &pf::kMorphModAmountLaw},
         {.name = "Morph Env Attack", .value = m.attackMs, .law = &pf::kMorphModTimeLaw},
         {.name = "Morph Env Decay", .value = m.decayMs, .law = &pf::kMorphModTimeLaw},
         {.name = "Morph Env Sustain", .value = m.sustain, .law = &pf::kMorphModSustainLaw},
         {.name = "Morph Cycle Rate", .value = m.cycleRateHz, .law = &pf::kMorphModRateLaw}}};
    for (const MmField& f : fields) {
        if (!Krate::DSP::detail::isFinite(f.value)) {
            return std::string(f.name) + " is not finite";
        }
        const auto v = static_cast<double>(f.value);
        if (v < f.law->lo || v > f.law->hi) {
            return std::string(f.name) + " " + drNum(v) + " outside [" + drNum(f.law->lo) + ", " +
                   drNum(f.law->hi) + "]";
        }
    }
    return {};
}

// FR-042 / C-14: a preset storing Source != Off claims no single-state column (a claim whose showcase plan at Morph
// Position 0.5 records the "kMorphPositionId=0" override, Phase 13 P-3; the probe's singleState rule).
std::string mmRuleSingleStateSource(const pd::ProfundumPresetDef& def,
                                    const ProfundumPluginTest::DecodedPresetState& d) {
    if (d.morphMod.source == 0) {
        return {};
    }
    ProfundumPluginTest::DecodedPresetState q = d;
    q.morph.position = 0.5f;
    for (const pd::Capability c : drClaims(def)) {
        if (c == pd::Capability::Count) {
            continue;   // rule 10's point
        }
        const ProfundumPluginTest::ShowcasePlan plan = ProfundumPluginTest::showcaseArmPlan(q, c);
        if (std::find(plan.overrides.begin(), plan.overrides.end(), "kMorphPositionId=0") != plan.overrides.end()) {
            return drLabel(c) + " is single-state but the preset stores Morph Mod Source " +
                   std::to_string(d.morphMod.source);
        }
    }
    return {};
}

/// "M-ENV" .. "M-WHEEL" for a MorphMod column; "" otherwise.
[[nodiscard]] std::string mmColumnId(pd::Capability c) {
    const int i = mme::mmColumnIndex(c);
    return i < 0 ? std::string{} : std::string(mme::kMmColumnIds[static_cast<std::size_t>(i)]);
}

[[nodiscard]] bool mmIsMorphModColumn(pd::Capability c) {
    const auto i = static_cast<std::size_t>(c);
    return i < pd::kNumCapabilities && pd::capabilityTable()[i].group == pd::CapabilityGroup::MorphMod;
}

/// The claims of a definition as the pre-phase harvest wrote them: the primary label, the secondary labels sorted and
/// joined by ';' (morph_mod_preset_test.cpp mmClaimsCsv).
[[nodiscard]] std::pair<std::string, std::string> mmClaimLabels(const pd::ProfundumPresetDef& def) {
    std::vector<std::string> secondaries;
    secondaries.reserve(def.secondaries.size());
    for (const pd::Capability c : def.secondaries) {
        secondaries.push_back(drLabel(c));
    }
    std::sort(secondaries.begin(), secondaries.end());
    std::string joined;
    for (std::size_t i = 0; i < secondaries.size(); ++i) {
        if (i > 0) {
            joined += ';';
        }
        joined += secondaries[i];
    }
    return {drLabel(def.primary), joined};
}

/// One pre-phase claims row (evidence/prephase-claims.csv).
struct MmPrePhaseClaim {
    std::string primary;
    std::string secondaries;
};

/// evidence/prephase-claims.csv keyed by preset name; ok false (with why) on a missing or malformed file.
[[nodiscard]] bool mmReadPrePhaseClaims(std::map<std::string, MmPrePhaseClaim>& out, std::string& why) {
    const mme::MmCsv csv = mme::mmReadCsv(mme::mmEvidenceDir() / "prephase-claims.csv");
    if (!csv.ok) {
        why = csv.why;
        return false;
    }
    const int name = csv.col("name");
    const int primary = csv.col("primaryLabel");
    const int secondaries = csv.col("secondaryLabels");
    if (name < 0 || primary < 0 || secondaries < 0) {
        why = "prephase-claims.csv: header lacks name / primaryLabel / secondaryLabels";
        return false;
    }
    for (const std::vector<std::string>& r : csv.rows) {
        if (r.size() != csv.header.size()) {
            why = "prephase-claims.csv: a row has " + std::to_string(r.size()) + " fields";
            return false;
        }
        const auto at = [&r](int c) { return r[static_cast<std::size_t>(c)]; };
        if (!out.emplace(at(name), MmPrePhaseClaim{.primary = at(primary), .secondaries = at(secondaries)}).second) {
            why = "prephase-claims.csv: preset \"" + at(name) + "\" appears twice";
            return false;
        }
    }
    return true;
}

// ---- plan 5.7 steps 2-4 re-derived from evidence/probe-morphmod.csv (evidence/probe/select.js, the same rules) ----

/// One candidate: the (preset, column, nine C-10 fields) of a probe row, with its E1 and C3 rows.
struct MmSelCandidate {
    std::string preset;
    int def = 0;
    std::string column;
    std::size_t order = 0;   // first appearance in the probe file
    const mme::MmProbeRow* e1 = nullptr;
    const mme::MmProbeRow* c3 = nullptr;
    bool carriable = false;
    double margin = 0.0;   // min(contrast_E1, contrast_C3) - 3 dB
};

struct MmSelPreset {
    std::string name;
    int def = 0;
    std::string primary;
    bool single = false;
};

// NOLINTNEXTLINE(bugprone-exception-escape) -- implicit special members of std::map/std::string members; test-only value type
struct MmSelection {
    bool ok = false;
    std::string why;
    std::vector<MmSelCandidate> candidates;
    std::map<std::string, std::size_t> converted;   // preset name -> winning candidate index
    std::map<std::string, std::size_t> newPreset;   // column id -> the best probe row's candidate (NEW PRESET NEEDED)
    std::vector<std::string> stops;                 // columns with no carriable candidate in any preset (FR-023)
};

/// select.js on the parsed probe: carriable = some candidate passes at both notes; a (preset, column)'s carrier is its
/// best-margin candidate (ties: file order); eligible = !singleState, not converted yet, and the preset's primary keeps
/// >= 1 other primary; rule PerColumn = (i), EveryCarrier = (ii); a preset converts at most once; a column left without
/// a conversion names its best carriable candidate (ties: definition order, then file order) as the new preset's base.
[[nodiscard]] MmSelection mmSelect(const mme::MmProbe& probe, pd::MorphModConversionRule rule) {
    MmSelection s;
    std::vector<MmSelPreset> presets;
    std::map<std::string, std::size_t> presetIndex;
    std::map<std::string, std::size_t> candidateIndex;
    for (const mme::MmProbeRow& r : probe.rows) {
        const auto pit = presetIndex.find(r.preset);
        if (pit == presetIndex.end()) {
            presetIndex.emplace(r.preset, presets.size());
            presets.push_back(
                MmSelPreset{.name = r.preset, .def = r.defIndex, .primary = r.primary, .single = r.singleState});
        } else {
            const MmSelPreset& p = presets[pit->second];
            if (p.def != r.defIndex || p.primary != r.primary || p.single != r.singleState) {
                s.why = "line " + std::to_string(r.line) + ": preset \"" + r.preset +
                        "\" changes defIndex / primary / singleState between rows";
                return s;
            }
        }
        std::string key = r.preset + '|' + r.column;
        for (const std::string& f : r.fieldText) {
            key += '|';
            key += f;
        }
        std::size_t k = 0;
        const auto cit = candidateIndex.find(key);
        if (cit == candidateIndex.end()) {
            k = s.candidates.size();
            candidateIndex.emplace(key, k);
            s.candidates.push_back(
                MmSelCandidate{.preset = r.preset, .def = r.defIndex, .column = r.column, .order = k});
        } else {
            k = cit->second;
        }
        MmSelCandidate& c = s.candidates[k];
        const mme::MmProbeRow** slot = nullptr;
        if (r.note == "E1")
            slot = &c.e1;
        else if (r.note == "C3")
            slot = &c.c3;
        if (slot == nullptr) {
            s.why = "line " + std::to_string(r.line) + ": unknown note \"" + r.note + "\"";
            return s;
        }
        if (*slot != nullptr) {
            s.why = "line " + std::to_string(r.line) + ": a second " + r.note + " row for candidate " + key;
            return s;
        }
        *slot = &r;
    }
    // Plan 10: evidence/gate-exclusions.csv (preset, column, gate, reading) names each (P, X) a Phase 13 gate measured
    // red; its candidates are not carriable (select.js does the same).
    std::set<std::string> excluded;
    {
        const mme::MmCsv ex = mme::mmReadCsv(mme::mmEvidenceDir() / "gate-exclusions.csv");
        if (!ex.ok || ex.col("preset") != 0 || ex.col("column") != 1 || ex.col("gate") != 2 || ex.col("reading") != 3) {
            s.why = "evidence/gate-exclusions.csv: " + (ex.ok ? std::string("bad header") : ex.why);
            return s;
        }
        for (const std::vector<std::string>& r : ex.rows) {
            if (r.size() != 4u || r[2].empty() || r[3].empty()) {
                s.why = "evidence/gate-exclusions.csv: a row without its gate and reading";
                return s;
            }
            excluded.insert(r[0] + '|' + r[1]);
        }
    }
    std::map<std::string, std::size_t> perPreset;
    for (MmSelCandidate& c : s.candidates) {
        if (c.e1 == nullptr || c.c3 == nullptr) {
            s.why = "candidate " + c.preset + " " + c.column + " lacks its E1 or C3 row";
            return s;
        }
        c.carriable = c.e1->pass && c.c3->pass && !excluded.contains(c.preset + '|' + c.column);
        c.margin = std::min(c.e1->contrast, c.c3->contrast) - 3.0;
        ++perPreset[c.preset];
    }
    for (const MmSelPreset& p : presets) {
        if (perPreset[p.name] != 16u) {
            s.why = "preset \"" + p.name + "\" has " + std::to_string(perPreset[p.name]) + " candidates, expected 16";
            return s;
        }
    }

    // Step 2: each (preset, column)'s best carriable candidate.
    std::map<std::string, std::size_t> carrier;
    for (std::size_t k = 0; k < s.candidates.size(); ++k) {
        const MmSelCandidate& c = s.candidates[k];
        if (!c.carriable) {
            continue;
        }
        const std::string key = c.preset + '|' + c.column;
        const auto it = carrier.find(key);
        if (it == carrier.end()) {
            carrier.emplace(key, k);
            continue;
        }
        const MmSelCandidate& cur = s.candidates[it->second];
        if (c.margin > cur.margin || (c.margin == cur.margin && c.order < cur.order)) {
            it->second = k;
        }
    }
    const auto byMarginThenDef = [&s](std::size_t a, std::size_t b) {
        const MmSelCandidate& x = s.candidates[a];
        const MmSelCandidate& y = s.candidates[b];
        return x.margin > y.margin || (x.margin == y.margin && x.def < y.def);
    };
    const auto carriersOf = [&](std::string_view column) {
        std::vector<std::size_t> out;
        for (const auto& [key, k] : carrier) {
            if (s.candidates[k].column == column) {
                out.push_back(k);
            }
        }
        std::stable_sort(out.begin(), out.end(), byMarginThenDef);
        return out;
    };

    // Steps 3-4.
    std::map<std::string, int> primaryCount;
    for (const MmSelPreset& p : presets) {
        ++primaryCount[p.primary];
    }
    const auto presetOf = [&](std::size_t k) -> const MmSelPreset& {
        return presets[presetIndex.at(s.candidates[k].preset)];
    };
    const auto eligible = [&](const MmSelPreset& p) {
        return !p.single && !s.converted.contains(p.name) && primaryCount[p.primary] >= 2;
    };
    const auto convert = [&](std::size_t k) {
        const MmSelPreset& p = presetOf(k);
        s.converted.emplace(p.name, k);
        --primaryCount[p.primary];
    };
    if (rule == pd::MorphModConversionRule::PerColumn) {
        for (const char* x : mme::kMmColumnIds) {
            for (const std::size_t k : carriersOf(x)) {
                if (eligible(presetOf(k))) {
                    convert(k);
                    break;
                }
            }
        }
    } else {
        std::vector<std::size_t> bestOf;
        for (const MmSelPreset& p : presets) {
            bool any = false;
            std::size_t best = 0;
            for (const char* x : mme::kMmColumnIds) {
                const auto it = carrier.find(p.name + '|' + x);
                if (it != carrier.end() && (!any || s.candidates[it->second].margin > s.candidates[best].margin)) {
                    best = it->second;
                    any = true;
                }
            }
            if (any) {
                bestOf.push_back(best);
            }
        }
        std::stable_sort(bestOf.begin(), bestOf.end(), byMarginThenDef);
        for (const std::size_t k : bestOf) {
            if (eligible(presetOf(k))) {
                convert(k);
            }
        }
    }

    for (const char* x : mme::kMmColumnIds) {
        bool hasConversion = false;
        for (const auto& [name, k] : s.converted) {
            hasConversion = hasConversion || s.candidates[k].column == x;
        }
        if (hasConversion) {
            continue;
        }
        std::vector<std::size_t> list = carriersOf(x);
        if (list.empty()) {
            s.stops.emplace_back(x);
            continue;
        }
        std::stable_sort(list.begin(), list.end(), [&s, &byMarginThenDef](std::size_t a, std::size_t b) {
            if (byMarginThenDef(a, b)) {
                return true;
            }
            if (byMarginThenDef(b, a)) {
                return false;
            }
            return s.candidates[a].order < s.candidates[b].order;
        });
        s.newPreset.emplace(x, list.front());
    }
    s.ok = true;
    return s;
}

}  // namespace

TEST_CASE("Profundum_PresetDefsRules", "[profundum][preset]") {
    const std::vector<pd::ProfundumPresetDef>& defs = pd::allPresets();
    REQUIRE(!defs.empty());

    // Rules 1-4, 6-10 on every library definition, built through the C-4 drive and decoded.
    std::vector<std::string> flagged;
    std::vector<ProfundumPluginTest::DecodedPresetState> decoded;   // Phase 14: one per definition, in order
    decoded.reserve(defs.size());
    for (const pd::ProfundumPresetDef& def : defs) {
        const ProfundumPluginTest::DecodedPresetState d = drDecodeOf(def);
        for (const DrNamedRule& r : drRules()) {
            const std::string why = r.fn(def, d);
            INFO("preset " << def.name << ", rule " << r.number << ": " << why);
            CHECK(why.empty());
        }
        // Phase 14 FR-042: the stored C-10 block inside its law box; no single-state claimant stores Source != Off.
        {
            const std::string law = mmRuleLawBox(d);
            INFO("preset " << def.name << ", FR-042 law box: " << law);
            CHECK(law.empty());
            const std::string single = mmRuleSingleStateSource(def, d);
            INFO("preset " << def.name << ", FR-042 single-state: " << single);
            CHECK(single.empty());
        }
        // Rule 5 (C-9): the fundamentalLossFlagged set is printed; the [.listen] case measures each member.
        if (ProfundumPluginTest::fundamentalLossFlagged(d)) {
            flagged.emplace_back(def.name);
        }
        decoded.push_back(d);
    }
    std::cout << "# Profundum_PresetDefsRules: presets=" << defs.size() << " fundamentalLossFlagged=" << flagged.size();
    for (const std::string& n : flagged) {
        std::cout << " \"" << n << '"';
    }
    std::cout << '\n' << std::flush;

    // ---- Phase 14 FR-063: every stored-Off definition keeps its pre-phase claims ----
    std::map<std::string, MmPrePhaseClaim> prePhase;
    {
        std::string why;
        const bool read = mmReadPrePhaseClaims(prePhase, why);
        INFO("evidence/prephase-claims.csv: " << why);
        REQUIRE(read);
        REQUIRE(!prePhase.empty());
    }
    std::string conversionsMd;
    {
        bool read = false;
        conversionsMd = mme::mmReadText(mme::mmEvidenceDir() / "conversions.md", read);
        INFO("evidence/conversions.md");
        REQUIRE(read);
    }
    std::map<std::string, std::string> libConverted;   // a pre-phase definition now primary in a MorphMod column
    std::vector<std::size_t> added;                    // definitions with no pre-phase row
    std::size_t kicks = 0;                             // Phase 14b: the Kicks category, added after Phase 14
    std::size_t coreTone = 0;                          // Phase 15: the S-STIFF / S-PHASE showcases
    for (std::size_t i = 0; i < defs.size(); ++i) {
        const pd::ProfundumPresetDef& def = defs[i];
        const ProfundumPluginTest::DecodedPresetState& d = decoded[i];
        const std::string name(def.name);
        const auto [primary, secondaries] = mmClaimLabels(def);
        INFO("preset " << name << ": primary \"" << primary << "\", secondaries \"" << secondaries << "\", Source "
                       << d.morphMod.source);
        if (def.category == "Kicks") {
            // Phase 14b (specs/Profundum-roadmap.md): no pre-phase row, Source Off, a Phase 13 primary.
            ++kicks;
            CHECK_FALSE(prePhase.contains(name));
            CHECK(def.morphModProbeBase.empty());
            CHECK(d.morphMod.source == 0);
            CHECK(!mmIsMorphModColumn(def.primary));
            continue;
        }
        if (pd::capabilityTable()[static_cast<std::size_t>(def.primary)].group == pd::CapabilityGroup::CoreTone) {
            // Phase 15 (specs/Profundum-roadmap.md): no pre-phase row, Source Off, an S-STIFF / S-PHASE primary.
            ++coreTone;
            CHECK_FALSE(prePhase.contains(name));
            CHECK(def.morphModProbeBase.empty());
            CHECK(d.morphMod.source == 0);
            continue;
        }
        const auto row = prePhase.find(name);
        if (row == prePhase.end()) {
            // An added preset: a MorphMod primary that conversions.md lists as NEW PRESET NEEDED on the probe's base.
            added.push_back(i);
            const std::string column = mmColumnId(def.primary);
            CHECK(mmIsMorphModColumn(def.primary));
            CHECK(!def.morphModProbeBase.empty());
            const std::string marker = "**" + column + ": NEW PRESET NEEDED.** Best probe row: base preset " +
                                       std::string(def.morphModProbeBase) + " (defIndex";
            INFO("conversions.md must hold: " << marker);
            CHECK(conversionsMd.find(marker) != std::string::npos);
            continue;
        }
        CHECK(def.morphModProbeBase.empty());   // only an added preset names a probe base
        if (d.morphMod.source == 0) {
            CHECK(row->second.primary == primary);
            CHECK(row->second.secondaries == secondaries);
            CHECK(!mmIsMorphModColumn(def.primary));
            continue;
        }
        // A converted preset (plan 5.7 step 5): primary moved to a MorphMod column, the old primary not re-claimed,
        // the secondaries unchanged.
        CHECK(mmIsMorphModColumn(def.primary));
        CHECK(row->second.secondaries == secondaries);
        CHECK(row->second.primary != primary);
        for (const pd::Capability c : def.secondaries) {
            CHECK(drLabel(c) != row->second.primary);
        }
        libConverted.emplace(name, mmColumnId(def.primary));
    }
    for (const auto& entry : prePhase) {
        const std::string& name = entry.first;
        INFO("pre-phase row \"" << name << "\" (" << entry.second.primary << ")");
        CHECK(std::any_of(defs.begin(), defs.end(),
                          [&name](const pd::ProfundumPresetDef& def) { return def.name == name; }));
    }

    // ---- Phase 14 P-9 (FR-046, SC-018): the converted set is kMorphModConversionRule applied to the probe ----
    static_assert(pd::kMorphModConversionRule == pd::MorphModConversionRule::EveryCarrier,
                  "R4 (evidence/plan-rulings.md, user 2026-10-26): rule (ii), every eligible carrier converts");
    const mme::MmProbe& probe = mme::mmProbe();
    INFO("evidence/probe-morphmod.csv: " << probe.why);
    REQUIRE(probe.ok);
    const MmSelection sel = mmSelect(probe, pd::kMorphModConversionRule);
    INFO("selection: " << sel.why);
    REQUIRE(sel.ok);
    CHECK(sel.stops.empty());   // FR-023: every column has a passing candidate somewhere
    std::map<std::string, std::string> selConverted;
    for (const auto& [name, k] : sel.converted) {
        selConverted.emplace(name, sel.candidates[k].column);
    }
    {
        std::string lib;
        std::string want;
        for (const auto& [n, c] : libConverted) {
            lib.append(" ").append(n).append(" -> ").append(c).append(";");
        }
        for (const auto& [n, c] : selConverted) {
            want.append(" ").append(n).append(" -> ").append(c).append(";");
        }
        INFO("library:" << lib << "\nprobe rule (ii):" << want);
        CHECK(libConverted == selConverted);
    }
    for (std::size_t i = 0; i < defs.size(); ++i) {
        const std::string name(defs[i].name);
        const auto it = sel.converted.find(name);
        if (it == sel.converted.end()) {
            continue;
        }
        // The winning candidate's nine fields are the stored ones (through the 1003-1011 laws).
        const MmMod& want = sel.candidates[it->second].e1->mod;
        INFO("converted " << name << ": stored Source " << decoded[i].morphMod.source << " Amount "
                          << decoded[i].morphMod.amount << "; winning Source " << want.source << " Amount "
                          << want.amount);
        CHECK(mme::mmSameSettings(decoded[i].morphMod, want));
    }
    std::set<std::string> addedColumns;
    for (const std::size_t i : added) {
        const pd::ProfundumPresetDef& def = defs[i];
        const std::string column = mmColumnId(def.primary);
        INFO("added " << def.name << " (" << column << ", base " << def.morphModProbeBase << ")");
        CHECK(addedColumns.insert(column).second);   // one new preset per column
        const auto it = sel.newPreset.find(column);
        REQUIRE(it != sel.newPreset.end());   // the column has no eligible carrier (FR-046)
        const MmSelCandidate& best = sel.candidates[it->second];
        CHECK(std::string(def.morphModProbeBase) == best.preset);
        CHECK(mme::mmSameSettings(decoded[i].morphMod, best.e1->mod));
    }
    CHECK(addedColumns.size() == sel.newPreset.size());
    CHECK(kicks == 11u);
    CHECK(coreTone == 2u);
    const MmSelection ruleI = mmSelect(probe, pd::MorphModConversionRule::PerColumn);   // comparison only
    std::cout << "# Profundum_PresetDefsRules: Phase 14 library size=" << defs.size()
              << " converted=" << libConverted.size() << " added=" << added.size()
              << " (probe rule (ii): " << sel.converted.size() << " conversions, " << sel.newPreset.size()
              << " new; rule (i) would convert " << ruleI.converted.size() << ")\n"
              << std::flush;

    // ---- non-vacuity: the base holds every rule; one test-local def per rule fails that rule's predicate ----
    const pd::ProfundumPresetDef base = drBaseDef();
    const ProfundumPluginTest::DecodedPresetState baseD = drDecodeOf(base);
    for (const DrNamedRule& r : drRules()) {
        const std::string why = r.fn(base, baseD);
        INFO("base, rule " << r.number << ": " << why);
        CHECK(why.empty());
    }

    std::vector<DrViolation> violations;
    const auto viaHost = [&violations](int rule, std::string label, const auto& mutate) {
        pd::ProfundumPresetDef d = drBaseDef();
        mutate(d);
        ProfundumPluginTest::DecodedPresetState dd = drDecodeOf(d);
        violations.push_back(DrViolation{.rule=rule, .label=std::move(label), .def=std::move(d), .decoded=std::move(dd)});
    };

    // 1: attack n = 0.85 -> 0.1 * 10^(5 * 0.85) = 1778 ms.
    viaHost(1, "attack 1778 ms",
            [](pd::ProfundumPresetDef& d) { d.endpointA.push_back({.id=::Profundum::kAmpAttackId, .normalized=0.85}); });
    // 2: the C-4 drive refuses IDs 0, 2, 3 and 417 in controls (P-6), so the violation is a decoded Output Gain.
    {
        ProfundumPluginTest::DecodedPresetState d = baseD;
        d.outputGain = 0.5f;
        violations.push_back(DrViolation{.rule=2, .label="Output Gain 0.5 (decoded)", .def=base, .decoded=d});
    }
    // 3: ENV source (index 1 of 3), polarity left Bipolar, no "resting offset" in the Comment.
    viaHost(3, "ENV Bipolar",
            [](pd::ProfundumPresetDef& d) { d.endpointA.push_back({.id=::Profundum::kMotionSourceId, .normalized=1.0 / 3.0}); });
    // 4: Rectify (2 / 5), Tone 1, Fold 100, WIDE (2 / 2).
    viaHost(4, "Rectify + Tone 1 + Fold 100 + WIDE", [](pd::ProfundumPresetDef& d) {
        d.endpointA.push_back({.id=::Profundum::kCharacterId, .normalized=0.4});
        d.endpointA.push_back({.id=::Profundum::kCharacterToneId, .normalized=1.0});
        d.endpointA.push_back({.id=::Profundum::kFoldAmountId, .normalized=1.0});
        d.endpointA.push_back({.id=::Profundum::kFieldImageModeId, .normalized=1.0});
    });
    // 6: the C-4 drive refuses a default name, so the violating def is checked against the base's decoded state.
    {
        pd::ProfundumPresetDef d = base;
        d.nameA = "A";
        violations.push_back(DrViolation{.rule=6, .label="name A \"A\"", .def=d, .decoded=baseD});
    }
    // 7: a single-state claim (AT-11) at Morph Position 0.5; an AT-33 claim on the default SPREAD 0.5 / depth 0.
    viaHost(7, "AT-11 claimed at p = 0.5", [](pd::ProfundumPresetDef& d) {
        d.secondaries = {pd::Capability::AT11};
        d.controls = {{.id=::Profundum::kMorphPositionId, .normalized=0.5}};
    });
    viaHost(7, "AT-33 claimed without spread 1 / depth 1",
            [](pd::ProfundumPresetDef& d) { d.secondaries = {pd::Capability::AT33}; });
    // 8: the AT-10 primary stores a Triangle Anchor (its row needs Sine, LOCK).
    viaHost(8, "AT-10 primary on Triangle",
            [](pd::ProfundumPresetDef& d) { d.endpointA.push_back({.id=::Profundum::kAnchorWaveformId, .normalized=0.25}); });
    // 9: Motion DRIFT (3 / 3) on the active endpoint with seedDependent false.
    viaHost(9, "DRIFT, seedDependent false", [](pd::ProfundumPresetDef& d) {
        d.endpointA.push_back({.id=::Profundum::kMotionSourceId, .normalized=1.0});
        d.seedDependent = false;
    });
    // 10: the primary repeated as a secondary.
    viaHost(10, "primary repeated as a secondary",
            [](pd::ProfundumPresetDef& d) { d.secondaries = {pd::Capability::AT10}; });

    std::set<int> covered;
    for (const DrViolation& v : violations) {
        const DrRule rule = drRuleOf(v.rule);
        REQUIRE(rule != nullptr);
        const std::string why = rule(v.def, v.decoded);
        INFO("rule " << v.rule << " non-vacuity (" << v.label << "): \"" << why << "\"");
        CHECK(!why.empty());
        covered.insert(v.rule);
    }
    const std::set<int> kAllRules{1, 2, 3, 4, 6, 7, 8, 9, 10};
    CHECK(covered == kAllRules);

    // Rule 3's exception: the same ENV Bipolar def with "resting offset" in its Comment holds the rule.
    {
        pd::ProfundumPresetDef d = drBaseDef();
        d.endpointA.push_back({.id=::Profundum::kMotionSourceId, .normalized=1.0 / 3.0});
        d.description = "test-local definition, Anchor Level, ENV as a resting offset";
        const ProfundumPluginTest::DecodedPresetState dd = drDecodeOf(d);
        REQUIRE(dd.a.motion.source == DrSrc::Env);
        REQUIRE(dd.a.motion.polarity == DrPol::Bipolar);
        CHECK(drRule3(d, dd).empty());
    }

    // Rule 5 predicate: true on one constructed flagged state, false on one unflagged state.
    {
        ProfundumPluginTest::DecodedPresetState on = baseD;
        on.a.characterAlgorithm = DrAlg::Rectify;
        on.a.characterAmount = 0.95f;
        on.a.anchor.level = 0.1f;
        REQUIRE(on.morph.position == 0.0f);
        CHECK(ProfundumPluginTest::fundamentalLossFlagged(on));
        ProfundumPluginTest::DecodedPresetState off = on;
        off.a.anchor.level = 0.5f;   // Anchor Level >= 0.25 keeps the fundamental
        CHECK_FALSE(ProfundumPluginTest::fundamentalLossFlagged(off));
        CHECK_FALSE(ProfundumPluginTest::fundamentalLossFlagged(baseD));
    }

    // Phase 14 FR-042 non-vacuity: the C-4 drive carries 1003-1011 (Source Envelope, Amount +0.6), the base holds
    // both predicates, an AT-11 claim (single-state) with that source fails, and planted out-of-box / non-finite
    // fields fail.
    {
        pd::ProfundumPresetDef env = drBaseDef();
        env.controls.push_back({.id = ::Profundum::kMorphModSourceId, .normalized = 0.25});   // Envelope (1 / 4)
        env.controls.push_back({.id = ::Profundum::kMorphModAmountId, .normalized = 0.8});    // Amount +0.6
        const ProfundumPluginTest::DecodedPresetState envD = drDecodeOf(env);
        REQUIRE(envD.morphMod.present);
        CHECK(envD.morphMod.source == 1);
        CHECK(std::abs(static_cast<double>(envD.morphMod.amount) - 0.6) <= 1.0e-6);
        CHECK(mmRuleLawBox(baseD).empty());
        CHECK(mmRuleLawBox(envD).empty());
        CHECK(mmRuleSingleStateSource(env, envD).empty());
        pd::ProfundumPresetDef single = env;
        single.secondaries = {pd::Capability::AT11};
        INFO("single-state claimant with Source Envelope: \"" << mmRuleSingleStateSource(single, envD) << "\"");
        CHECK(!mmRuleSingleStateSource(single, envD).empty());
        CHECK(mmRuleSingleStateSource(single, baseD).empty());   // Source Off: the claim is allowed

        ProfundumPluginTest::DecodedPresetState bad = envD;
        bad.morphMod.source = 5;
        CHECK(!mmRuleLawBox(bad).empty());
        bad = envD;
        bad.morphMod.cycleSyncRate = 30;
        CHECK(!mmRuleLawBox(bad).empty());
        bad = envD;
        bad.morphMod.amount = 1.5f;
        CHECK(!mmRuleLawBox(bad).empty());
        bad = envD;
        volatile std::uint32_t nanBits = 0x7FC00000u;
        bad.morphMod.decayMs = std::bit_cast<float>(static_cast<std::uint32_t>(nanBits));
        CHECK(!mmRuleLawBox(bad).empty());
    }
}

// =============================================================================
// T024 (FR-001 - FR-006, FR-008, FR-009, FR-011, FR-024, FR-033 - FR-036, FR-060; SC-001 - SC-006, SC-012, SC-030):
// the per-push library checks. Every case reads the committed tree under PROFUNDUM_PRESETS_DIR (or allPresets()) and
// is red until T026 lands the 53-file tree. Expectations are independent literals, never the writer under test
// (buildProfundumInfoXml).
// =============================================================================

#ifndef PROFUNDUM_PRESETS_DIR
#error "PROFUNDUM_PRESETS_DIR must be defined (plugins/profundum/tests/CMakeLists.txt)"
#endif

namespace {

namespace libfs = std::filesystem;

[[nodiscard]] libfs::path libPresetsRoot() {
    return {PROFUNDUM_PRESETS_DIR};
}

/// Every committed *.vstpreset under the presets root, sorted.
[[nodiscard]] std::vector<libfs::path> libCommittedPresetFiles() {
    std::vector<libfs::path> out;
    std::error_code ec;
    for (const libfs::directory_entry& e : libfs::recursive_directory_iterator(libPresetsRoot(), ec)) {
        std::error_code typeEc;
        if (e.is_regular_file(typeEc) && e.path().extension() == ".vstpreset") {
            out.push_back(e.path());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

/// Every regular file under @p root, keyed by its generic relative path.
[[nodiscard]] std::map<std::string, libfs::path> libRelativeFiles(const libfs::path& root) {
    std::map<std::string, libfs::path> out;
    std::error_code ec;
    for (const libfs::directory_entry& e : libfs::recursive_directory_iterator(root, ec)) {
        std::error_code typeEc;
        if (e.is_regular_file(typeEc)) {
            out[e.path().lexically_relative(root).generic_string()] = e.path();
        }
    }
    return out;
}

[[nodiscard]] std::uint64_t libLe64(const std::vector<std::uint8_t>& b, std::size_t at) {
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < 8u; ++i) {
        v |= static_cast<std::uint64_t>(b[at + i]) << (8u * i);
    }
    return v;
}

[[nodiscard]] std::uint32_t libLe32(const std::vector<std::uint8_t>& b, std::size_t at) {
    return static_cast<std::uint32_t>(b[at]) | (static_cast<std::uint32_t>(b[at + 1]) << 8) |
           (static_cast<std::uint32_t>(b[at + 2]) << 16) | (static_cast<std::uint32_t>(b[at + 3]) << 24);
}

/// One parsed .vstpreset: the 48-byte header, the chunk List, and the Comp / Info chunks.
struct LibPresetFile {
    bool ok = false;
    std::string why;
    std::string classId;  // the 32 ASCII characters at offset 8
    std::vector<std::uint8_t> comp;
    std::string info;
};

/// An independent reader of the .vstpreset container (never the generator's writer).
[[nodiscard]] LibPresetFile libReadPresetFile(const libfs::path& path) {
    LibPresetFile out;
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        out.why = "cannot open";
        return out;
    }
    const std::vector<std::uint8_t> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    constexpr std::size_t kHeader = 48;
    if (b.size() < kHeader) {
        out.why = "file is " + std::to_string(b.size()) + " bytes, shorter than the 48-byte header";
        return out;
    }
    if (std::memcmp(b.data(), "VST3", 4) != 0) {
        out.why = "no VST3 magic";
        return out;
    }
    if (libLe32(b, 4) != 1u) {
        out.why = "header version " + std::to_string(libLe32(b, 4)) + " != 1";
        return out;
    }
    out.classId.assign(reinterpret_cast<const char*>(b.data() + 8), 32);
    const std::uint64_t listOffset = libLe64(b, 40);
    if (listOffset < kHeader || listOffset + 8u > b.size()) {
        out.why = "list offset " + std::to_string(listOffset) + " outside the file";
        return out;
    }
    const auto list = static_cast<std::size_t>(listOffset);
    if (std::memcmp(b.data() + list, "List", 4) != 0) {
        out.why = "no List chunk at the list offset";
        return out;
    }
    const std::uint32_t count = libLe32(b, list + 4);
    constexpr std::size_t kEntry = 20;  // id 4 + offset 8 + size 8
    if (list + 8u + static_cast<std::size_t>(count) * kEntry > b.size()) {
        out.why = "List of " + std::to_string(count) + " entries runs past the end of the file";
        return out;
    }
    bool haveComp = false;
    bool haveInfo = false;
    for (std::uint32_t k = 0; k < count; ++k) {
        const std::size_t at = list + 8u + static_cast<std::size_t>(k) * kEntry;
        const std::string id(reinterpret_cast<const char*>(b.data() + at), 4);
        const std::uint64_t off = libLe64(b, at + 4);
        const std::uint64_t size = libLe64(b, at + 12);
        if (off < kHeader || off > b.size() || size > b.size() - off) {
            out.why = "chunk " + id + " (offset " + std::to_string(off) + ", size " + std::to_string(size) +
                      ") lies outside the file";
            return out;
        }
        const auto first = b.begin() + static_cast<std::ptrdiff_t>(off);
        const auto last = first + static_cast<std::ptrdiff_t>(size);
        if (id == "Comp") {
            out.comp.assign(first, last);
            haveComp = true;
        } else if (id == "Info") {
            out.info.assign(reinterpret_cast<const char*>(b.data()) + static_cast<std::size_t>(off),
                            static_cast<std::size_t>(size));
            haveInfo = true;
        }
    }
    if (!haveComp || !haveInfo) {
        out.why = std::string("missing chunk:") + (haveComp ? "" : " Comp") + (haveInfo ? "" : " Info");
        return out;
    }
    out.ok = true;
    return out;
}

struct LibInfoAttr {
    std::string id;
    std::string value;
};

/// The value of key="..." inside one <Attr .../> tag; empty when absent.
[[nodiscard]] std::string libTagValue(std::string_view tag, std::string_view key) {
    const std::string needle = " " + std::string(key) + "=\"";
    const std::size_t at = tag.find(needle);
    if (at == std::string_view::npos) {
        return {};
    }
    const std::size_t from = at + needle.size();
    const std::size_t to = tag.find('"', from);
    if (to == std::string_view::npos) {
        return {};
    }
    return std::string(tag.substr(from, to - from));
}

/// Every <Attr id="..." value="..." .../> of an Info chunk, in order.
[[nodiscard]] std::vector<LibInfoAttr> libInfoAttrs(std::string_view xml) {
    std::vector<LibInfoAttr> out;
    std::size_t pos = 0;
    while ((pos = xml.find("<Attr ", pos)) != std::string_view::npos) {
        const std::size_t end = xml.find("/>", pos);
        if (end == std::string_view::npos) {
            break;
        }
        const std::string_view tag = xml.substr(pos, end - pos);
        out.push_back(LibInfoAttr{.id=libTagValue(tag, "id"), .value=libTagValue(tag, "value")});
        pos = end + 2;
    }
    return out;
}

/// The words of a capabilityTable() label after its "AT-nn" / "R-..." prefix.
[[nodiscard]] std::vector<std::string> libLabelWords(pd::Capability c) {
    std::vector<std::string> words;
    const auto i = static_cast<std::size_t>(c);
    if (i >= pd::kNumCapabilities) {
        return words;
    }
    const std::string_view label = pd::capabilityTable()[i].label;
    std::size_t pos = label.find(' ');
    if (pos == std::string_view::npos) {
        return words;
    }
    ++pos;
    while (pos < label.size()) {
        std::size_t end = label.find(' ', pos);
        if (end == std::string_view::npos) {
            end = label.size();
        }
        if (end > pos) {
            words.emplace_back(label.substr(pos, end - pos));
        }
        pos = end + 1;
    }
    return words;
}

[[nodiscard]] const pd::ProfundumPresetDef* libDefNamed(std::string_view name) {
    for (const pd::ProfundumPresetDef& d : pd::allPresets()) {
        if (d.name == name) {
            return &d;
        }
    }
    return nullptr;
}

/// The names of every decoded float field that is NaN or Inf (bit test, never std::isfinite).
[[nodiscard]] std::vector<std::string> libNonFiniteFields(const ProfundumPluginTest::DecodedPresetState& d) {
    std::vector<std::string> out;
    const auto check = [&out](const std::string& name, float v) {
        if (!ProfundumPluginTest::presetFloatIsFinite(v)) {
            out.push_back(name);
        }
    };
    check("outputGain", d.outputGain);
    check("outputCeilingDb", d.outputCeilingDb);
    for (std::size_t i = 0; i < d.macros.size(); ++i) {
        check("macros[" + std::to_string(i) + "]", d.macros[i]);
    }
    check("note.portamentoMs", d.note.portamentoMs);
    check("note.bendSt", d.note.bendSt);
    check("morph.position", d.morph.position);
    check("morph.rampMs", d.morph.rampMs);
    check("runtime.target", d.runtime.target);
    check("runtime.position", d.runtime.position);
    check("runtime.anchorPosition", d.runtime.anchorPosition);
    check("runtime.rampMs", d.runtime.rampMs);
    const std::array<std::pair<const char*, const Krate::DSP::MorphState*>, 2> ends{{{"a", &d.a}, {"b", &d.b}}};
    for (const auto& [endpoint, s] : ends) {
        const ::Profundum::LeafBits bits = ::Profundum::leafBitsOf(*s);
        for (std::size_t i = 0; i < ::Profundum::kNumLeaves; ++i) {
            const auto& info = ::Profundum::lawOf(static_cast<std::uint8_t>(i));
            if (info.kind == Krate::DSP::MorphState::Kind::F32 &&
                !ProfundumPluginTest::presetFloatIsFinite(std::bit_cast<float>(bits[i]))) {
                out.push_back(std::string(endpoint) + "." + info.name);
            }
        }
    }
    return out;
}

[[nodiscard]] std::string libJoin(const std::vector<std::string>& items) {
    std::string s;
    for (const std::string& i : items) {
        s += "\n  ";
        s += i;
    }
    return s;
}

}  // namespace

// SC-001 (FR-001, FR-002): the directory set equals the seven names (Phase 14b: Kicks) both ways; only *.vstpreset files, each directly
// inside one category directory; 0 stray files (no .gitkeep).
TEST_CASE("Profundum_PresetCategoriesMatchFilesystem", "[profundum][preset]") {
    const std::set<std::string> kSix{"Sub", "Resonant", "Harmonic", "Driven", "Plucked", "Animated", "Kicks"};
    const libfs::path root = libPresetsRoot();
    INFO("presets root: " << root.string());
    std::error_code ec;
    REQUIRE(libfs::is_directory(root, ec));

    std::set<std::string> dirs;
    std::vector<std::string> strays;
    for (const libfs::directory_entry& e : libfs::directory_iterator(root, ec)) {
        std::error_code typeEc;
        if (e.is_directory(typeEc)) {
            dirs.insert(e.path().filename().string());
        } else {
            strays.push_back(e.path().filename().string() + " (not a category directory)");
        }
    }
    REQUIRE_FALSE(ec);
    CHECK(dirs == kSix);
    const std::vector<std::string>& configured = ::Profundum::makeProfundumPresetConfig().subcategoryNames;
    CHECK(std::set<std::string>(configured.begin(), configured.end()) == dirs);

    std::size_t presets = 0;
    for (const libfs::directory_entry& e : libfs::recursive_directory_iterator(root, ec)) {
        const libfs::path rel = e.path().lexically_relative(root);
        const auto depth = std::distance(rel.begin(), rel.end());
        std::error_code typeEc;
        if (e.is_directory(typeEc)) {
            if (depth != 1) {
                strays.push_back(rel.generic_string() + " (nested directory)");
            }
            continue;
        }
        if (depth == 1) {
            continue;  // already reported by the top-level walk
        }
        const bool regular = e.is_regular_file(typeEc);
        const bool directlyInCategory = depth == 2 && kSix.contains(rel.begin()->string());
        if (regular && directlyInCategory && e.path().extension() == ".vstpreset") {
            ++presets;
        } else {
            strays.push_back(rel.generic_string());
        }
    }
    REQUIRE_FALSE(ec);
    INFO("stray entries:" << libJoin(strays));
    CHECK(strays.empty());
    CHECK(presets > 0u);  // non-vacuity: the walk saw the library
}

// SC-002 (FR-003, FR-033): every file parses; class ID == kProcessorUID; exactly seven Info attributes, each against an
// independent expectation; the Comment names its primary column in words.
TEST_CASE("Profundum_FactoryPresetContainer", "[profundum][preset]") {
    // Non-vacuity of the readers.
    REQUIRE(libLabelWords(pd::Capability::AT04) == std::vector<std::string>{"Character", "Tube"});
    REQUIRE(libLabelWords(pd::Capability::RSolid) == std::vector<std::string>{"Reference", "Solid"});
    {
        const std::vector<LibInfoAttr> probe =
            libInfoAttrs("<MetaInfo>\n  <Attr id=\"Name\" value=\"X Y\" type=\"string\"/>\n</MetaInfo>\n");
        REQUIRE(probe.size() == 1u);
        REQUIRE(probe[0].id == "Name");
        REQUIRE(probe[0].value == "X Y");
    }

    const std::vector<libfs::path> files = libCommittedPresetFiles();
    REQUIRE_FALSE(files.empty());
    for (const libfs::path& path : files) {
        INFO("preset " << path.string());
        const LibPresetFile pf = libReadPresetFile(path);
        INFO("why: " << pf.why);
        CHECK(pf.ok);
        if (!pf.ok) {
            continue;
        }
        Steinberg::FUID fileUid;
        const bool uidParsed = fileUid.fromString(pf.classId.c_str());
        CHECK(uidParsed);
        const bool uidMatches = fileUid == ::Profundum::kProcessorUID;
        CHECK(uidMatches);

        const std::vector<LibInfoAttr> attrs = libInfoAttrs(pf.info);
        CHECK(attrs.size() == 7u);
        std::map<std::string, std::string> byId;
        for (const LibInfoAttr& a : attrs) {
            byId[a.id] = a.value;
        }
        CHECK(byId.size() == attrs.size());  // no attribute repeated
        const auto attr = [&byId](const char* id) {
            const auto it = byId.find(id);
            return it == byId.end() ? std::string("<missing>") : it->second;
        };
        const std::string stem = path.stem().string();
        const std::string dir = path.parent_path().filename().string();
        CHECK(attr("MediaType") == "VstPreset");
        CHECK(attr("PlugInName") == "Profundum");
        CHECK(attr("PlugInCategory") == "Synth");
        CHECK(attr("Name") == stem);
        CHECK(attr("MusicalCategory") == dir);
        CHECK(attr("MusicalInstrument") == dir);

        const auto commentIt = byId.find("Comment");
        const std::string comment = commentIt == byId.end() ? std::string{} : commentIt->second;
        CHECK_FALSE(comment.empty());
        const pd::ProfundumPresetDef* def = libDefNamed(stem);
        CHECK(def != nullptr);
        if (def == nullptr) {
            continue;
        }
        const std::string lowerComment = presetLower(comment);
        const std::vector<std::string> words = libLabelWords(def->primary);
        CHECK_FALSE(words.empty());
        for (const std::string& w : words) {
            INFO("primary " << drLabel(def->primary) << ", word \"" << w << "\", Comment \"" << comment << '"');
            CHECK(lowerComment.find(presetLower(w)) != std::string::npos);
        }
    }
}

// SC-003 (FR-004, FR-005): 6 categories, each >= 3 presets; N >= 53 (53 expected); unique, valid names.
TEST_CASE("Profundum_FactoryLibraryShape", "[profundum][preset]") {
    // Non-vacuity: the name predicate rejects a filesystem-hostile name.
    REQUIRE_FALSE(Krate::Plugins::PresetManager::isValidPresetName("Bad/Name"));
    REQUIRE(Krate::Plugins::PresetManager::isValidPresetName("Mono Bedrock"));

    const std::vector<pd::ProfundumPresetDef>& defs = pd::allPresets();
    CHECK(pd::kCategories.size() == 7u);   // Phase 14b: Kicks
    std::map<std::string, std::size_t> perCategory;
    for (const std::string_view c : pd::kCategories) {
        perCategory[std::string(c)] = 0;
    }
    std::set<std::string> names;
    for (const pd::ProfundumPresetDef& def : defs) {
        const std::string name(def.name);
        const std::string category(def.category);
        INFO("preset " << name << " (" << category << ")");
        CHECK(perCategory.contains(category));
        ++perCategory[category];
        CHECK(names.insert(name).second);
        CHECK(Krate::Plugins::PresetManager::isValidPresetName(name));
    }
    CHECK(perCategory.size() == 7u);
    for (const auto& [category, n] : perCategory) {
        INFO("category " << category);
        CHECK(n >= 3u);
    }
    CHECK(defs.size() >= 53u);
    // Phase 14 T023 (FR-046, SC-018): 58 + the two presets added for M-CYCLE and M-VEL, which no existing preset could
    // carry (evidence/conversions.md, NEW PRESET NEEDED rows; Long Tail carries M-WHEEL, evidence/gate-exclusions.csv).
    // Phase 14b: + the eleven presets of the Kicks category (definitions 61-71). Phase 15: + Stiff Wire and Phase Bite
    // (definitions 72-73), the S-STIFF and S-PHASE showcases.
    CHECK(defs.size() == 73u);

    // The committed tree has the same shape.
    std::map<std::string, std::size_t> perDir;
    const std::vector<libfs::path> files = libCommittedPresetFiles();
    for (const libfs::path& p : files) {
        ++perDir[p.parent_path().filename().string()];
    }
    CHECK(files.size() == defs.size());
    for (const std::string_view c : pd::kCategories) {
        const std::string category(c);
        INFO("directory " << category);
        CHECK(perDir[category] >= 3u);
    }
}

// SC-004 (FR-006, FR-008, FR-047, FR-049): every Comp is v3, 610 bytes (Phase 14), all floats finite, Edit Target A, the runtime
// target and ramped position at the stored Morph Position, the declared seed.
TEST_CASE("Profundum_FactoryPresetStateFormat", "[profundum][preset]") {
    // Non-vacuity: the finiteness sweep sees a NaN planted in a decoded field.
    {
        ProfundumPluginTest::DecodedPresetState probe{};
        REQUIRE(libNonFiniteFields(probe).empty());
        volatile std::uint32_t nanBits = 0x7FC00000u;
        probe.outputGain = std::bit_cast<float>(static_cast<std::uint32_t>(nanBits));
        REQUIRE(libNonFiniteFields(probe) == std::vector<std::string>{"outputGain"});
    }

    const std::vector<libfs::path> files = libCommittedPresetFiles();
    REQUIRE_FALSE(files.empty());
    for (const libfs::path& path : files) {
        INFO("preset " << path.string());
        const LibPresetFile pf = libReadPresetFile(path);
        INFO("why: " << pf.why);
        CHECK(pf.ok);
        if (!pf.ok) {
            continue;
        }
        const ProfundumPluginTest::DecodedPresetState d = ProfundumPluginTest::decodePresetState(pf.comp);
        CHECK(d.ok);
        CHECK(d.version == 3);
        // Phase 14 T024 (SC-007, FR-061): 610 bytes (kStateV3Bytes, plugin_ids.h) and the first int32 read straight
        // from the bytes (little-endian) as the literal 3, not through the version symbol.
        static_assert(::Profundum::kStateV3Bytes == 626u, "SC-007: every factory Comp is 626 bytes (Phase 15; Phase 14: 610)");
        CHECK(d.bytes == ::Profundum::kStateV3Bytes);
        CHECK(pf.comp.size() == ::Profundum::kStateV3Bytes);
        REQUIRE(pf.comp.size() >= 4u);
        const auto firstWord = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(pf.comp[0]) | (static_cast<std::uint32_t>(pf.comp[1]) << 8u) |
            (static_cast<std::uint32_t>(pf.comp[2]) << 16u) | (static_cast<std::uint32_t>(pf.comp[3]) << 24u));
        CHECK(firstWord == 3);
        const std::vector<std::string> nonFinite = libNonFiniteFields(d);
        INFO("non-finite fields:" << libJoin(nonFinite));
        CHECK(nonFinite.empty());
        CHECK(d.morph.editTarget == 0);
        CHECK(std::abs(static_cast<double>(d.runtime.target) - static_cast<double>(d.morph.position)) <= 2.5e-4);
        CHECK(std::abs(static_cast<double>(d.runtime.position) - static_cast<double>(d.morph.position)) <= 2.5e-4);
        const pd::ProfundumPresetDef* def = libDefNamed(path.stem().string());
        CHECK(def != nullptr);
        if (def != nullptr) {
            CHECK(d.seed == def->seed);
        }
    }
}

// SC-005 (FR-009, FR-034): PresetHost load -> save decodes equal within 2.5e-4; names byte-equal, never the defaults.
TEST_CASE("Profundum_FactoryPresetRoundTrip", "[profundum][preset]") {
    const std::vector<libfs::path> files = libCommittedPresetFiles();
    REQUIRE_FALSE(files.empty());
    for (const libfs::path& path : files) {
        INFO("preset " << path.string());
        const LibPresetFile pf = libReadPresetFile(path);
        INFO("why: " << pf.why);
        CHECK(pf.ok);
        if (!pf.ok) {
            continue;
        }
        auto host = std::make_unique<ProfundumPluginTest::PresetHost>();
        REQUIRE(host->prepare(48000.0, 512) == Steinberg::kResultOk);
        CHECK(host->loadState(pf.comp) == Steinberg::kResultOk);
        std::vector<std::uint8_t> saved;
        CHECK(host->saveState(saved));
        const ProfundumPluginTest::DecodedPresetState in = ProfundumPluginTest::decodePresetState(pf.comp);
        const ProfundumPluginTest::DecodedPresetState out = ProfundumPluginTest::decodePresetState(saved);
        CHECK(in.ok);
        CHECK(out.ok);
        std::string where;
        const bool same = ProfundumPluginTest::decodedEqual(out, in, 2.5e-4, where);
        INFO("first difference: " << where);
        CHECK(same);
        CHECK(out.names[0] == in.names[0]);
        CHECK(out.names[1] == in.names[1]);
        CHECK(out.names[0] != "A");
        CHECK(out.names[1] != "B");
    }
}

// SC-006 (FR-010, FR-011): 53 / 53 columns have a primary; every definition exactly one primary; AT-13 claimed on a
// stored Rectify AND on a stored Fracture (P-2).
// Phase 14 T023 (FR-040, SC-018): 57 columns (the four MorphMod columns appended after R-SINE), 0 empty; every one of
// the 53 pre-existing columns still has a primary after the conversions.
TEST_CASE("Profundum_CoverageMatrixComplete", "[profundum][preset]") {
    static_assert(pd::kNumCapabilities == 59u, "Phase 14 FR-040: 53 Phase 13 columns + M-ENV, M-CYCLE, M-VEL, M-WHEEL; "
                                               "Phase 15: + S-STIFF, S-PHASE");
    static_assert(static_cast<std::size_t>(pd::Capability::MEnv) == 53u,
                  "the MorphMod columns are appended: every Phase 13 column keeps its index");
    constexpr std::size_t kPrePhaseColumns = 53;
    const std::vector<pd::ProfundumPresetDef>& defs = pd::allPresets();
    REQUIRE_FALSE(defs.empty());

    std::array<std::size_t, pd::kNumCapabilities> primaries{};
    bool at13Rectify = false;
    bool at13Fracture = false;
    for (const pd::ProfundumPresetDef& def : defs) {
        INFO("preset " << def.name);
        const auto idx = static_cast<std::size_t>(def.primary);
        CHECK(idx < pd::kNumCapabilities);
        if (idx < pd::kNumCapabilities) {
            ++primaries[idx];
        }
        CHECK(std::count(def.secondaries.begin(), def.secondaries.end(), def.primary) == 0);

        const std::vector<pd::Capability> claims = drClaims(def);
        if (std::find(claims.begin(), claims.end(), pd::Capability::AT13) != claims.end()) {
            const ProfundumPluginTest::DecodedPresetState d = drDecodeOf(def);
            const ProfundumPluginTest::ShowcasePlan plan =
                ProfundumPluginTest::showcaseArmPlan(d, pd::Capability::AT13);
            INFO("AT-13 claim, variant \"" << plan.variant << "\"");
            CHECK(plan.applicable);
            at13Rectify = at13Rectify || (plan.applicable && plan.variant == "Rectify");
            at13Fracture = at13Fracture || (plan.applicable && plan.variant == "Fracture");
        }
    }

    std::size_t covered = 0;
    for (std::size_t i = 0; i < pd::kNumCapabilities; ++i) {
        INFO("column " << pd::capabilityTable()[i].label);
        CHECK(primaries[i] >= 1u);
        covered += primaries[i] >= 1u ? 1u : 0u;
    }
    CHECK(covered == pd::kNumCapabilities);   // 59, 0 empty
    std::size_t preExisting = 0;
    for (std::size_t i = 0; i < kPrePhaseColumns; ++i) {
        preExisting += primaries[i] >= 1u ? 1u : 0u;
    }
    CHECK(preExisting == kPrePhaseColumns);   // every Phase 13 column still covered after the conversions
    std::size_t morphMod = 0;
    for (std::size_t i = 0; i < pd::kNumCapabilities; ++i) {
        if (pd::capabilityTable()[i].group == pd::CapabilityGroup::MorphMod) {
            ++morphMod;
            INFO("MorphMod column " << pd::capabilityTable()[i].label);
            CHECK(i >= kPrePhaseColumns);
        }
    }
    CHECK(morphMod == 4u);
    std::cout << "# Profundum_CoverageMatrixComplete: presets=" << defs.size() << " columns=" << pd::kNumCapabilities
              << " covered=" << covered << " preExistingCovered=" << preExisting << '\n'
              << std::flush;
    CHECK(at13Rectify);
    CHECK(at13Fracture);
}

// SC-012 (FR-036): the committed tree equals a fresh writeFactoryTree: same relative paths, Info byte-equal, every Comp
// decodedEqual within 2.5e-4.
TEST_CASE("Profundum_CommittedPresetTreeMatches", "[profundum][preset]") {
    const TempDirGuard gen(libfs::temp_directory_path() /
                           ("profundum-tree-match-" + std::to_string(presetTestPid())));
    std::size_t written = 0;
    std::string why;
    const bool ok = ProfundumPluginTest::writeFactoryTree(gen.path, written, why);
    INFO("why: " << why);
    REQUIRE(ok);

    const std::map<std::string, libfs::path> committed = libRelativeFiles(libPresetsRoot());
    const std::map<std::string, libfs::path> generated = libRelativeFiles(gen.path);
    REQUIRE_FALSE(generated.empty());
    std::vector<std::string> onlyCommitted;
    std::vector<std::string> onlyGenerated;
    for (const auto& entry : committed) {
        if (!generated.contains(entry.first)) {
            onlyCommitted.push_back(entry.first);
        }
    }
    for (const auto& entry : generated) {
        if (!committed.contains(entry.first)) {
            onlyGenerated.push_back(entry.first);
        }
    }
    INFO("only committed:" << libJoin(onlyCommitted) << "\nonly generated:" << libJoin(onlyGenerated));
    CHECK(onlyCommitted.empty());
    CHECK(onlyGenerated.empty());
    CHECK(generated.size() == written);

    for (const auto& [rel, genPath] : generated) {
        const auto it = committed.find(rel);
        if (it == committed.end()) {
            continue;
        }
        INFO("file " << rel);
        const LibPresetFile c = libReadPresetFile(it->second);
        const LibPresetFile g = libReadPresetFile(genPath);
        INFO("committed why: " << c.why << "; generated why: " << g.why);
        CHECK(c.ok);
        CHECK(g.ok);
        if (!c.ok || !g.ok) {
            continue;
        }
        CHECK(c.info == g.info);
        const ProfundumPluginTest::DecodedPresetState dc = ProfundumPluginTest::decodePresetState(c.comp);
        const ProfundumPluginTest::DecodedPresetState dg = ProfundumPluginTest::decodePresetState(g.comp);
        std::string where;
        const bool same = ProfundumPluginTest::decodedEqual(dc, dg, 2.5e-4, where);
        INFO("first difference: " << where);
        CHECK(same);
    }
}

// SC-030 (FR-060): every decoded Comp stores the -1 dBTP Output Ceiling.
TEST_CASE("Profundum_FactoryPresetCeiling", "[profundum][preset]") {
    const std::vector<libfs::path> files = libCommittedPresetFiles();
    REQUIRE_FALSE(files.empty());
    std::size_t atCeiling = 0;
    for (const libfs::path& path : files) {
        INFO("preset " << path.string());
        const LibPresetFile pf = libReadPresetFile(path);
        INFO("why: " << pf.why);
        CHECK(pf.ok);
        if (!pf.ok) {
            continue;
        }
        const ProfundumPluginTest::DecodedPresetState d = ProfundumPluginTest::decodePresetState(pf.comp);
        const bool held = std::abs(static_cast<double>(d.outputCeilingDb) + 1.0) <= 1.0e-6;
        INFO("ceiling " << d.outputCeilingDb << " dBTP");
        CHECK(held);
        atCeiling += held ? 1u : 0u;
    }
    CHECK(atCeiling == files.size());
}

// SC-034 (FR-066, Phase 13b): at least 5 factory presets store Voice Mode Poly with Polyphony 4, and none of them leads with a
// column whose contrast requires Mono (AT-23 legato glide, AT-43 to AT-46 note memory).
TEST_CASE("Profundum_PolyPresetCount", "[profundum][preset]") {
    namespace pd = ::Profundum::PresetDefs;
    using C = pd::Capability;
    using ProfundumPluginTest::decodePresetState;
    using ProfundumPluginTest::DecodedPresetState;
    using ProfundumPluginTest::presetCompOf;
    std::size_t poly = 0;
    for (const pd::ProfundumPresetDef& def : pd::allPresets()) {
        const DecodedPresetState d = decodePresetState(presetCompOf(def));
        INFO("preset " << def.name);
        REQUIRE(d.ok);
        if (d.voiceMode != 1) {
            continue;
        }
        ++poly;
        CHECK(d.polyphony == 4);
        CHECK(def.primary != C::AT23);
        CHECK(def.primary != C::AT43);
        CHECK(def.primary != C::AT44);
        CHECK(def.primary != C::AT45);
        CHECK(def.primary != C::AT46);
    }
    CHECK(poly >= 5u);
}

// Library breadth (2026-10-26, the designed-library pass): the factory library loads as designed sounds, not as one lever
// over registered defaults. Counted on endpoint A, the sound a preset loads with. Before the pass: Fold 0 on 78 %, the Sine
// Anchor on 91 %, Character Amount 0 on 61 %.
TEST_CASE("Profundum_PresetLibraryBreadth", "[profundum][preset]") {
    namespace pd = ::Profundum::PresetDefs;
    using Alg = Krate::DSP::BassCharacterStage::Algorithm;
    using Img = Krate::DSP::BassStereoField::ImageMode;
    using AW = Krate::DSP::AnchorOscillator::Waveform;
    using ProfundumPluginTest::decodePresetState;
    using ProfundumPluginTest::DecodedPresetState;
    using ProfundumPluginTest::presetCompOf;
    constexpr double kMaxFoldOffShare = 0.30;     // Harmonic Fold 0 on at most 30 % of the library
    constexpr double kMaxSineAnchorShare = 0.50;  // the Sine Anchor on at most half of it
    constexpr float kCharacterOnAmount = 0.15f;   // a Character algorithm counts as used from Amount 0.15
    constexpr std::size_t kMinPerAlgorithm = 6;
    constexpr std::size_t kMinPerImageMode = 4;
    constexpr std::size_t kMinPerAnchorWaveform = 4;

    const std::vector<pd::ProfundumPresetDef>& defs = pd::allPresets();
    REQUIRE(!defs.empty());
    std::size_t foldOff = 0;
    std::array<std::size_t, 6> perAlgorithm{};
    std::array<std::size_t, 3> perImageMode{};
    std::array<std::size_t, 5> perWaveform{};
    for (const pd::ProfundumPresetDef& def : defs) {
        const DecodedPresetState d = decodePresetState(presetCompOf(def));
        INFO("preset " << def.name);
        REQUIRE(d.ok);
        foldOff += d.a.foldAmount <= 0.0f ? 1u : 0u;
        if (d.a.characterAmount >= kCharacterOnAmount) {
            ++perAlgorithm[static_cast<std::size_t>(d.a.characterAlgorithm)];
        }
        ++perImageMode[static_cast<std::size_t>(d.a.field.mode)];
        ++perWaveform[static_cast<std::size_t>(d.a.anchor.waveform)];
    }

    const double n = static_cast<double>(defs.size());
    const std::array<const char*, 6> algNames{"Tube", "Fold", "Rectify", "Bite", "Crush", "Fracture"};
    const std::array<const char*, 3> imageNames{"Solid", "Spread", "Wide"};
    const std::array<const char*, 5> waveNames{"Sine", "Triangle", "RoundedSine", "AsymmetricSine", "OctaveDownSine"};
    std::cout << "# Profundum_PresetLibraryBreadth: presets=" << defs.size() << " fold_off=" << foldOff
              << " sine_anchor=" << perWaveform[static_cast<std::size_t>(AW::Sine)] << '\n';
    for (std::size_t i = 0; i < algNames.size(); ++i) {
        std::cout << "#   character " << algNames[i] << ": " << perAlgorithm[i] << '\n';
        INFO("Character " << algNames[i] << " at Amount >= " << kCharacterOnAmount << " on endpoint A");
        CHECK(perAlgorithm[i] >= kMinPerAlgorithm);
    }
    for (std::size_t i = 0; i < imageNames.size(); ++i) {
        std::cout << "#   image " << imageNames[i] << ": " << perImageMode[i] << '\n';
        INFO("image mode " << imageNames[i] << " on endpoint A");
        CHECK(perImageMode[i] >= kMinPerImageMode);
    }
    for (std::size_t i = 0; i < waveNames.size(); ++i) {
        std::cout << "#   anchor " << waveNames[i] << ": " << perWaveform[i] << '\n';
        INFO("Anchor waveform " << waveNames[i] << " on endpoint A");
        CHECK(perWaveform[i] >= kMinPerAnchorWaveform);
    }
    std::cout << std::flush;
    INFO("Harmonic Fold 0 on endpoint A: " << foldOff << " of " << defs.size());
    CHECK(static_cast<double>(foldOff) <= kMaxFoldOffShare * n);
    INFO("Sine Anchor on endpoint A: " << perWaveform[static_cast<std::size_t>(AW::Sine)] << " of " << defs.size());
    CHECK(static_cast<double>(perWaveform[static_cast<std::size_t>(AW::Sine)]) <= kMaxSineAnchorShare * n);
    static_assert(static_cast<std::size_t>(Alg::Fracture) == 5 && static_cast<std::size_t>(Img::Wide) == 2 &&
                  static_cast<std::size_t>(AW::OctaveDownSine) == 4);
}
