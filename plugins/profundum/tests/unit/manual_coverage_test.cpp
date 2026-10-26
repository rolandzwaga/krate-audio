// Profundum Phase 13 manual coverage test (specs/profundum-phase13-presets-release/tasks.md)
//
// Placeholder created by T007 (Deviation 1). Case written by T028: Profundum_ManualCoversSurface (FR-051, FR-052; SC-024).
//
// MANUAL.md must name every user-visible registered parameter title verbatim (all registered IDs except the hidden
// performance IDs 4-7, ui_strings_test_table.h kHiddenPerformanceIds), the six preset categories and every factory
// preset name; docs/index.html must exist and link the manual. The titles are read from a heap ControllerFixture,
// never from a hand-written list, so a parameter added later without manual text turns this case red.

#include <catch2/catch_test_macros.hpp>
#include "profundum_test_fixture.h"

#include "preset/profundum_preset_config.h"
#include "profundum_preset_defs.h"
#include "ui_strings_test_table.h"

#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] std::string manualReadFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.good()) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

[[nodiscard]] std::string manualAsciiOf(const Steinberg::Vst::TChar* s) {
    char buf[256] = {};
    Steinberg::UString(const_cast<Steinberg::Vst::TChar*>(s), 128)  // NOLINT(cppcoreguidelines-pro-type-const-cast)
        .toAscii(buf, 256);
    return {buf};
}

/// The predicate under test: every entry of @p names that does not appear verbatim in @p text.
[[nodiscard]] std::vector<std::string> manualMissing(std::string_view text, const std::vector<std::string>& names) {
    std::vector<std::string> missing;
    for (const std::string& n : names) {
        if (text.find(n) == std::string_view::npos) {
            missing.push_back(n);
        }
    }
    return missing;
}

/// Every occurrence of @p what removed from @p text.
[[nodiscard]] std::string manualErase(std::string text, std::string_view what) {
    std::size_t at = text.find(what);
    while (at != std::string::npos) {
        text.erase(at, what.size());
        at = text.find(what);
    }
    return text;
}

}  // namespace

// T028 (FR-051, FR-052; SC-024).
TEST_CASE("Profundum_ManualCoversSurface", "[profundum][docs]") {
    const std::string manual = manualReadFile(std::string(PROFUNDUM_RESOURCES_DIR) + "/../MANUAL.md");
    REQUIRE_FALSE(manual.empty());

    // ---- every user-visible registered title (all registered IDs except 4-7) ----
    std::vector<std::string> titles;
    {
        ProfundumPluginTest::ControllerFixture cf;
        const Steinberg::int32 n = cf.controller->getParameterCount();
        REQUIRE(n == 84);
        for (Steinberg::int32 i = 0; i < n; ++i) {
            Steinberg::Vst::ParameterInfo info{};
            REQUIRE(cf.controller->getParameterInfo(i, info) == Steinberg::kResultOk);
            const auto id = static_cast<std::int32_t>(info.id);
            if (std::find(ProfundumPluginTest::kHiddenPerformanceIds.begin(),
                          ProfundumPluginTest::kHiddenPerformanceIds.end(),
                          id) != ProfundumPluginTest::kHiddenPerformanceIds.end()) {
                continue;
            }
            std::string title = manualAsciiOf(info.title);
            REQUIRE_FALSE(title.empty());
            titles.push_back(std::move(title));
        }
    }
    REQUIRE(titles.size() == 80);
    {
        const std::vector<std::string> missing = manualMissing(manual, titles);
        for (const std::string& m : missing) {
            FAIL_CHECK("title missing from MANUAL.md: " << m);
        }
        CHECK(missing.empty());
    }

    // ---- the six category names ----
    const Krate::Plugins::PresetManagerConfig config = ::Profundum::makeProfundumPresetConfig();
    REQUIRE(config.subcategoryNames.size() == 6);
    {
        const std::vector<std::string> missing = manualMissing(manual, config.subcategoryNames);
        for (const std::string& m : missing) {
            FAIL_CHECK("category missing from MANUAL.md: " << m);
        }
        CHECK(missing.empty());
    }

    // ---- all 58 factory preset names ----
    std::vector<std::string> presetNames;
    for (const auto& def : ::Profundum::PresetDefs::allPresets()) {
        presetNames.emplace_back(def.name);
    }
    REQUIRE(presetNames.size() == 58);
    {
        const std::vector<std::string> missing = manualMissing(manual, presetNames);
        for (const std::string& m : missing) {
            FAIL_CHECK("preset missing from MANUAL.md: " << m);
        }
        CHECK(missing.empty());
    }

    // ---- docs/index.html exists and links the manual ----
    const std::string index = manualReadFile(std::string(PROFUNDUM_RESOURCES_DIR) + "/../docs/index.html");
    REQUIRE_FALSE(index.empty());
    CHECK(index.find("MANUAL") != std::string::npos);

    // ---- non-vacuity: the same predicate on a copy with one title removed reports exactly that title ----
    const std::string removed = "Excitation Asymmetry";
    REQUIRE(std::find(titles.begin(), titles.end(), removed) != titles.end());
    const std::vector<std::string> missingAfter = manualMissing(manualErase(manual, removed), titles);
    REQUIRE(missingAfter.size() == 1);
    CHECK(missingAfter.front() == removed);
}
