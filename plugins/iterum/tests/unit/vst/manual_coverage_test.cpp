// ==============================================================================
// Iterum_ManualCoversSurface: MANUAL.md documents the whole user-facing surface
// ==============================================================================
// MANUAL.md must name every user-facing parameter title, every preset category (the ten modes) and every factory
// preset (as the browser shows it: the file name). Titles come from the controller and preset names from
// resources/presets/, never from a hand-written list, so a parameter or preset added without manual text turns this
// case red.
//
// "Custom Time 1" .. "Custom Time 16" and "Custom Level 1" .. "Custom Level 16" (the MultiTap custom pattern, edited
// in the tap editor) are checked as "Custom Time" and "Custom Level". Hidden and read-only parameters have no control
// and are not checked.

#include "controller/controller.h"
#include "preset/iterum_preset_config.h"

#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

[[nodiscard]] std::string readText(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.good()) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

[[nodiscard]] std::string asciiOf(const Steinberg::Vst::TChar* s) {
    char buf[256] = {};
    Steinberg::UString(const_cast<Steinberg::Vst::TChar*>(s), 128)  // NOLINT(cppcoreguidelines-pro-type-const-cast)
        .toAscii(buf, 256);
    return {buf};
}

/// What MANUAL.md must contain for the parameter @p title (see the file header).
[[nodiscard]] std::string manualKey(const std::string& title) {
    static const std::regex custom(R"(^(Custom (Time|Level)) [0-9]+$)");
    std::smatch m;
    if (std::regex_match(title, m, custom)) {
        return m[1].str();
    }
    return title;
}

/// Every entry of @p names that does not appear verbatim in @p text.
[[nodiscard]] std::vector<std::string> missingFrom(std::string_view text, const std::vector<std::string>& names) {
    std::vector<std::string> missing;
    for (const std::string& n : names) {
        if (text.find(n) == std::string_view::npos) {
            missing.push_back(n);
        }
    }
    return missing;
}

void reportMissing(const char* what, const std::vector<std::string>& missing) {
    for (const std::string& m : missing) {
        FAIL_CHECK(what << " missing from MANUAL.md: " << m);
    }
    CHECK(missing.empty());
}

}  // namespace

TEST_CASE("Iterum_ManualCoversSurface", "[iterum][docs]") {
    const std::filesystem::path pluginDir = std::filesystem::path(ITERUM_RESOURCES_DIR) / "..";
    const std::string manual = readText(pluginDir / "MANUAL.md");
    REQUIRE_FALSE(manual.empty());

    SECTION("the key of a custom tap title is its family") {
        CHECK(manualKey("Custom Time 12") == "Custom Time");
        CHECK(manualKey("Custom Level 1") == "Custom Level");
        CHECK(manualKey("Tape Head 1 Level") == "Tape Head 1 Level");
    }

    // ---- every user-facing parameter title ----
    std::set<std::string> keySet;
    {
        auto controller = Steinberg::owned(new Iterum::Controller());
        REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);
        const Steinberg::int32 n = controller->getParameterCount();
        REQUIRE(n > 100);
        for (Steinberg::int32 i = 0; i < n; ++i) {
            Steinberg::Vst::ParameterInfo info{};
            REQUIRE(controller->getParameterInfo(i, info) == Steinberg::kResultOk);
            if ((info.flags & (Steinberg::Vst::ParameterInfo::kIsHidden | Steinberg::Vst::ParameterInfo::kIsReadOnly)) != 0) {
                continue;
            }
            const std::string title = asciiOf(info.title);
            REQUIRE_FALSE(title.empty());
            keySet.insert(manualKey(title));
        }
        REQUIRE(controller->terminate() == Steinberg::kResultOk);
    }
    const std::vector<std::string> keys(keySet.begin(), keySet.end());
    reportMissing("parameter", missingFrom(manual, keys));

    // ---- the preset categories (the ten modes) and every factory preset ----
    const std::vector<std::string> categories = Iterum::makeIterumPresetConfig().subcategoryNames;
    REQUIRE(categories.size() == 10u);
    reportMissing("category", missingFrom(manual, categories));

    std::vector<std::string> presets;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(std::filesystem::path(ITERUM_RESOURCES_DIR) / "presets")) {
        if (entry.is_regular_file() && entry.path().extension() == ".vstpreset") {
            presets.push_back(entry.path().stem().string());
        }
    }
    std::sort(presets.begin(), presets.end());
    REQUIRE(presets.size() == 101u);
    reportMissing("preset", missingFrom(manual, presets));

    // ---- non-vacuity: the predicate on a copy with one key removed reports exactly that key ----
    const std::string removed = "Tape Motor Inertia";
    REQUIRE(std::find(keys.begin(), keys.end(), removed) != keys.end());
    std::string without = manual;
    for (std::size_t at = without.find(removed); at != std::string::npos; at = without.find(removed)) {
        without.erase(at, removed.size());
    }
    const std::vector<std::string> after = missingFrom(without, keys);
    REQUIRE(after.size() == 1u);
    CHECK(after.front() == removed);
}
