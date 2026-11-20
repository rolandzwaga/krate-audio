// ==============================================================================
// Disrumpo_ManualCoversSurface: MANUAL.md documents the whole user-facing surface
// ==============================================================================
// MANUAL.md must name every user-facing parameter title, the 26 distortion types, every preset category and every
// factory preset (as the browser shows it: the file name). Titles come from the controller and preset names from
// resources/presets/, never from a hand-written list, so a parameter or preset added without manual text turns this
// case red.
//
// Per-band and per-node titles repeat one control four or sixteen times ("Band 2 Gain", "B3 N1 Drive", "B1 N2 Shape 7"),
// so the manual names each control once and explains the numbering: such a title is checked by what follows its band /
// node prefix, with a trailing slot number dropped ("Gain", "Drive", "Shape"). The custom sweep curve's "Curve P0 X" ..
// "Curve P7 Y" are checked as "Curve P". Hidden and read-only parameters have no control and are not checked.

#include "controller/controller.h"
#include "dsp/distortion_types.h"
#include "preset/disrumpo_preset_config.h"

#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
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
    static const std::regex band(R"(^Band [1-4] (.+)$)");
    static const std::regex node(R"(^B[1-4] N[1-4] (.+?)( [0-9]+)?$)");
    static const std::regex curvePoint(R"(^Curve P[0-7] [XY]$)");
    std::smatch m;
    if (std::regex_match(title, m, band)) {
        return m[1].str();
    }
    if (std::regex_match(title, m, node)) {
        return m[1].str();
    }
    if (std::regex_match(title, curvePoint)) {
        return "Curve P";
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

TEST_CASE("Disrumpo_ManualCoversSurface", "[disrumpo][docs]") {
    const std::filesystem::path pluginDir = std::filesystem::path(DISRUMPO_RESOURCES_DIR) / "..";
    const std::string manual = readText(pluginDir / "MANUAL.md");
    REQUIRE_FALSE(manual.empty());

    SECTION("the key of a per-band or per-node title is the control's name") {
        CHECK(manualKey("Band 3 Morph X Link") == "Morph X Link");
        CHECK(manualKey("B2 N4 Drive") == "Drive");
        CHECK(manualKey("B1 N1 Shape 7") == "Shape");
        CHECK(manualKey("Curve P5 Y") == "Curve P");
        CHECK(manualKey("LFO 1 Rate") == "LFO 1 Rate");
    }

    // ---- every user-facing parameter title ----
    std::set<std::string> keySet;
    {
        auto controller = std::make_unique<Disrumpo::Controller>();
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

    // ---- the 26 distortion types ----
    std::vector<std::string> types;
    for (int t = 0; t < Disrumpo::kDistortionTypeCount; ++t) {
        types.emplace_back(Disrumpo::getTypeName(static_cast<Disrumpo::DistortionType>(t)));
    }
    REQUIRE(types.size() == 26u);
    reportMissing("distortion type", missingFrom(manual, types));

    // ---- the preset categories and every factory preset ----
    const std::vector<std::string> categories = Disrumpo::makeDisrumpoPresetConfig().subcategoryNames;
    REQUIRE(categories.size() == 11u);
    reportMissing("category", missingFrom(manual, categories));

    std::vector<std::string> presets;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(std::filesystem::path(DISRUMPO_RESOURCES_DIR) / "presets")) {
        if (entry.is_regular_file() && entry.path().extension() == ".vstpreset") {
            presets.push_back(entry.path().stem().string());
        }
    }
    std::sort(presets.begin(), presets.end());
    REQUIRE(presets.size() == 120u);
    reportMissing("preset", missingFrom(manual, presets));

    // ---- non-vacuity: the predicate on a copy with one key removed reports exactly that key ----
    const std::string removed = "Sweep Falloff";
    REQUIRE(std::find(keys.begin(), keys.end(), removed) != keys.end());
    std::string without = manual;
    for (std::size_t at = without.find(removed); at != std::string::npos; at = without.find(removed)) {
        without.erase(at, removed.size());
    }
    const std::vector<std::string> after = missingFrom(without, keys);
    REQUIRE(after.size() == 1u);
    CHECK(after.front() == removed);
}
