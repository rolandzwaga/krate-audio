// ==============================================================================
// Preset descriptions: read back from the Info chunk and shown in the browser
// (Profundum Phase 13 T037; FR-065, SC-033 (a) and (c))
// ==============================================================================

#include "preset/preset_manager.h"
#include "preset/preset_manager_config.h"
#include "ui/preset_browser_view.h"

#include "public.sdk/source/common/memorystream.h"
#include "test_helpers/editor_lifecycle_harness.h"   // ensureVstguiInitialized (the browser builds VSTGUI views)

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

Krate::Plugins::PresetManagerConfig descTestConfig() {
    return Krate::Plugins::PresetManagerConfig{
        .processorUID = Steinberg::FUID(0x0D35C001, 0x0D35C002, 0x0D35C003, 0x0D35C004),
        .pluginName = "DescTest",
        .pluginCategoryDesc = "Synth",
        .subcategoryNames = {"Alpha", "Beta"}};
}

/// A unique user / factory directory pair, removed on destruction.
class DescTestDirs {
public:
    DescTestDirs() {
        std::random_device rd;
        std::ostringstream name;
        name << "krate_preset_desc_" << std::uniform_int_distribution<>(100000, 999999)(rd);
        root_ = fs::temp_directory_path() / name.str();
        fs::create_directories(root_ / "user");
        fs::create_directories(root_ / "factory");
    }
    ~DescTestDirs() {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }
    DescTestDirs(const DescTestDirs&) = delete;
    DescTestDirs& operator=(const DescTestDirs&) = delete;
    DescTestDirs(DescTestDirs&&) = delete;
    DescTestDirs& operator=(DescTestDirs&&) = delete;

    [[nodiscard]] fs::path user() const { return root_ / "user"; }
    [[nodiscard]] fs::path factory() const { return root_ / "factory"; }

private:
    fs::path root_;
};

/// A manager over the test directories whose state provider returns a small component stream.
Krate::Plugins::PresetManager makeDescManager(const DescTestDirs& dirs) {
    Krate::Plugins::PresetManager manager(descTestConfig(), nullptr, nullptr, dirs.user(), dirs.factory());
    manager.setStateProvider([]() -> Steinberg::IBStream* {
        auto* stream = new Steinberg::MemoryStream();
        char payload[8] = {'d', 'e', 's', 'c', 't', 'e', 's', 't'};
        Steinberg::int32 written = 0;
        stream->write(payload, 8, &written);
        stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
        return stream;
    });
    return manager;
}

const Krate::Plugins::PresetInfo* findPreset(const Krate::Plugins::PresetManager::PresetList& list, const std::string& name) {
    for (const auto& p : list) {
        if (p.name == name) {
            return &p;
        }
    }
    return nullptr;
}

std::vector<char> readAll(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void writeAll(const fs::path& path, const std::vector<char>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

}  // namespace

TEST_CASE("Krate_PresetManagerReadsDescription", "[preset][shared][description]") {
    const DescTestDirs dirs;
    auto manager = makeDescManager(dirs);
    const std::string special = R"(Round "warm" bass & <growl> it's)";

    REQUIRE(manager.savePreset("Described", "Alpha", "A plain description."));
    REQUIRE(manager.savePreset("Special", "Alpha", special));
    REQUIRE(manager.savePreset("Undescribed", "Beta", ""));

    SECTION("a saved description scans back identically, special characters included") {
        const auto list = manager.scanPresets();
        const auto* described = findPreset(list, "Described");
        const auto* withSpecial = findPreset(list, "Special");
        const auto* none = findPreset(list, "Undescribed");
        REQUIRE(described != nullptr);
        REQUIRE(withSpecial != nullptr);
        REQUIRE(none != nullptr);
        CHECK(described->description == "A plain description.");
        CHECK(withSpecial->description == special);
        CHECK(none->description.empty());
        // Name and category still come from the filename and folder.
        CHECK(described->category == "Alpha");
        CHECK(none->category == "Beta");
    }

    SECTION("a truncated file scans with an empty description and its name and category unchanged") {
        const fs::path path = dirs.user() / "Alpha" / "Described.vstpreset";
        std::vector<char> bytes = readAll(path);
        REQUIRE(bytes.size() > 64u);
        bytes.resize(bytes.size() / 2);  // the chunk list at the end is gone
        writeAll(path, bytes);
        const auto list = manager.scanPresets();
        const auto* p = findPreset(list, "Described");
        REQUIRE(p != nullptr);
        CHECK(p->description.empty());
        CHECK(p->category == "Alpha");
    }

    SECTION("a chunk list offset past the end scans with an empty description") {
        const fs::path path = dirs.user() / "Alpha" / "Described.vstpreset";
        std::vector<char> bytes = readAll(path);
        REQUIRE(bytes.size() > 48u);
        // Header: 'VST3', int32 version, 32-byte class ID, int64 chunk-list offset at byte 40.
        const std::int64_t past = static_cast<std::int64_t>(bytes.size()) + 4096;
        std::memcpy(bytes.data() + 40, &past, sizeof(past));
        writeAll(path, bytes);
        const auto list = manager.scanPresets();
        const auto* p = findPreset(list, "Described");
        REQUIRE(p != nullptr);
        CHECK(p->description.empty());
        CHECK(p->category == "Alpha");
    }
}

TEST_CASE("Krate_PresetBrowserShowsDescription", "[preset][shared][description][ui]") {
    Krate::TestSupport::ensureVstguiInitialized();
    const DescTestDirs dirs;
    auto manager = makeDescManager(dirs);
    // One preset per category, so each tab holds exactly one row and the test never depends on scan order.
    REQUIRE(manager.savePreset("Described", "Alpha", "Deep and round."));
    REQUIRE(manager.savePreset("Undescribed", "Beta", ""));

    Krate::Plugins::PresetBrowserView view(VSTGUI::CRect(0, 0, 960, 600), &manager, {"All", "Alpha", "Beta"});
    view.open("Alpha");
    CHECK(view.descriptionTextForTest().empty());  // nothing selected yet

    view.onPresetSelected(0);  // the only Alpha row
    REQUIRE(view.descriptionTextForTest() == "Deep and round.");

    SECTION("a tab change clears it, and a preset without one shows nothing") {
        view.onCategoryTabChanged(1);  // filter index 1 = tabLabels_[2] = Beta (preset_browser_view.cpp:309-311)
        CHECK(view.descriptionTextForTest().empty());
        view.onPresetSelected(0);  // the only Beta row: Undescribed
        CHECK(view.descriptionTextForTest().empty());
    }
    SECTION("a search change clears it") {
        view.onSearchTextChanged("Desc");
        CHECK(view.descriptionTextForTest().empty());
    }
    view.close();
}
