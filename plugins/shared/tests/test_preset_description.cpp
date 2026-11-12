// ==============================================================================
// Preset descriptions: read back from the Info chunk and shown in the browser
// (Profundum Phase 13 T037; FR-065, SC-033 (a) and (c))
// ==============================================================================

#include "preset/preset_manager.h"
#include "preset/preset_manager_config.h"
#include "ui/preset_browser_view.h"
#include "ui/preset_description_layout.h"

#include "public.sdk/source/common/memorystream.h"
#include "test_helpers/editor_lifecycle_harness.h"   // ensureVstguiInitialized (the browser builds VSTGUI views)

#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/platform/iplatformfont.h"
#include "vstgui/lib/platform/platformfactory.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#if SMTG_OS_WINDOWS
#include <objbase.h>   // CoInitializeEx: Direct2D offscreen bitmaps are WIC bitmaps, which need COM (a host has it)
#endif

namespace {

namespace fs = std::filesystem;

Krate::Plugins::PresetManagerConfig descTestConfig() {
    return Krate::Plugins::PresetManagerConfig{
        .processorUID = Steinberg::FUID(0x0D35C001, 0x0D35C002, 0x0D35C003, 0x0D35C004),
        .pluginName = "DescTest",
        .pluginCategoryDesc = "Synth",
        .subcategoryNames = {"Alpha", "Beta", "Gamma"}};
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

// Two real Disrumpo factory descriptions (tools/disrumpo_preset_generator.cpp), verbatim: the longest (189 characters,
// "Envelope Follower") and the shortest (116, "Init 1 Band").
constexpr const char* kLongDescription =
    "The sweep follows your playing level instead of an LFO, rising as you play harder over Tape, Tube and Soft Clip "
    "bands; the input level also drives the Tube mids. For guitar, keys and drums.";
constexpr const char* kShortDescription =
    "One full-range Soft Clip band at drive 1, with no sweep and no modulation: the blank starting point for a new "
    "sound.";
constexpr const char* kEllipsis = "\xE2\x80\xA6";  // U+2026

/// The browser's description area: its one direct CTextLabel child that is not the search field.
VSTGUI::CTextLabel* descriptionLabelOf(Krate::Plugins::PresetBrowserView& view) {
    VSTGUI::CTextLabel* found = nullptr;
    view.forEachChild([&](VSTGUI::CView* child) {
        if (auto* label = dynamic_cast<VSTGUI::CTextLabel*>(child);
            label != nullptr && dynamic_cast<VSTGUI::CTextEdit*>(child) == nullptr) {
            found = label;
        }
    });
    return found;
}

template <typename T>
T* directChildOf(Krate::Plugins::PresetBrowserView& view) {
    T* found = nullptr;
    view.forEachChild([&](VSTGUI::CView* child) {
        if (found == nullptr) {
            found = dynamic_cast<T*>(child);
        }
    });
    return found;
}

/// The view's tooltip text (empty when it has none), as CTooltipSupport reads it (kCViewTooltipAttribute).
std::string tooltipOf(VSTGUI::CView& view) {
    std::uint32_t size = 0;
    if (!view.getAttributeSize(VSTGUI::kCViewTooltipAttribute, size) || size == 0) {
        return {};
    }
    std::vector<char> buffer(size + 1, '\0');
    std::uint32_t read = 0;
    if (!view.getAttribute(VSTGUI::kCViewTooltipAttribute, size, buffer.data(), read)) {
        return {};
    }
    return {buffer.data()};
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line, '\n');) {
        lines.push_back(line);
    }
    return lines;
}

bool endsWith(const std::string& s, const std::string& tail) {
    return s.ends_with(tail);
}

/// The width the label's own font gives @p text (the measure CMultiLineTextLabel lays its lines out with).
double textWidth(const VSTGUI::CTextLabel& label, const std::string& text) {
    const auto* painter = label.getFont()->getFontPainter();
    REQUIRE(painter != nullptr);
    return painter->getStringWidth(nullptr, VSTGUI::UTF8String(text).getPlatformString(), true);
}

double textAreaWidth(const VSTGUI::CTextLabel& label) {
    return label.getWidth() - (2.0 * label.getTextInset().x);
}

/// Renders @p view (at the origin) at 2x into a PNG; returns the byte count written (0 on failure).
std::size_t writeBrowserShot(VSTGUI::CView& view, const fs::path& file) {
    const VSTGUI::CRect rect = view.getViewSize();
    auto bitmap = VSTGUI::renderBitmapOffscreen(rect.getSize(), 2.0,
                                                [&](VSTGUI::CDrawContext& ctx) { view.drawRect(&ctx, rect); });
    if (bitmap == nullptr || bitmap->getPlatformBitmap() == nullptr) {
        return 0;
    }
    const VSTGUI::PNGBitmapBuffer png =
        VSTGUI::getPlatformFactory().createBitmapMemoryPNGRepresentation(bitmap->getPlatformBitmap());
    if (png.empty()) {
        return 0;
    }
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary);
    out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    return out ? png.size() : 0;
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

// FR-065 layout, platform-independent: one width unit per code point stands in for the font measure.
TEST_CASE("Krate_DescriptionLayoutWrapsAtWordsWithEllipsis", "[preset][shared][description]") {
    using Krate::Plugins::wrapDescriptionLines;
    const auto codePoints = [](const std::string& s) {
        double n = 0.0;
        for (const char c : s) {
            if ((static_cast<unsigned char>(c) & 0xC0u) != 0x80u) {
                n += 1.0;
            }
        }
        return n;
    };

    SECTION("text that fits one line is returned unchanged") {
        CHECK(wrapDescriptionLines(std::string("Deep and round."), 20.0, 3, codePoints) == "Deep and round.");
    }
    SECTION("empty or blank text gives nothing") {
        CHECK(wrapDescriptionLines(std::string(), 20.0, 3, codePoints).empty());
        CHECK(wrapDescriptionLines(std::string("   "), 20.0, 3, codePoints).empty());
    }
    SECTION("text wraps at spaces, never inside a word, and keeps every word when it fits") {
        // 'aaaa bbbb cccc dddd eeee' at width 9: two words per line.
        CHECK(wrapDescriptionLines(std::string("aaaa bbbb cccc dddd eeee"), 9.0, 3, codePoints) ==
              "aaaa bbbb\ncccc dddd\neeee");
    }
    SECTION("text that needs more lines stops at the last one with an ellipsis that fits") {
        // Line 3 would be 'eeee ffff'; with the ellipsis it is 10 wide, so 'ffff' goes: 'eeee…'.
        CHECK(wrapDescriptionLines(std::string("aaaa bbbb cccc dddd eeee ffff gggg"), 9.0, 3, codePoints) ==
              "aaaa bbbb\ncccc dddd\neeee\xE2\x80\xA6");
        // Line 3 'eeee' plus the ellipsis fits as it is.
        CHECK(wrapDescriptionLines(std::string("aaaa bbbb cccc dddd eeee ffffffff"), 9.0, 3, codePoints) ==
              "aaaa bbbb\ncccc dddd\neeee\xE2\x80\xA6");
        // One line allowed.
        CHECK(wrapDescriptionLines(std::string("aaaa bbbb cccc"), 9.0, 1, codePoints) == "aaaa\xE2\x80\xA6");
    }
    SECTION("a last line that is one word too wide for the ellipsis loses characters, whole code points only") {
        CHECK(wrapDescriptionLines(std::string("\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9 bb"), 4.0, 1, codePoints) ==
              "\xC3\xA9\xC3\xA9\xC3\xA9\xE2\x80\xA6");
    }
}

// FR-065 (user ruling 2026-11-12, "wrap + hover"): the description area holds up to three lines, wrapped at spaces,
// the last ending with an ellipsis when the text still does not fit; the full text is the area's tooltip.
TEST_CASE("Krate_PresetBrowserDescriptionWrapsWithTooltip", "[preset][shared][description][ui]") {
    Krate::TestSupport::ensureVstguiInitialized();
    const DescTestDirs dirs;
    auto manager = makeDescManager(dirs);
    // One preset per category, so each tab holds exactly one row.
    REQUIRE(manager.savePreset("Envelope Follower", "Alpha", kLongDescription));
    REQUIRE(manager.savePreset("Init 1 Band", "Beta", kShortDescription));
    REQUIRE(manager.savePreset("Undescribed", "Gamma", ""));
    const std::vector<std::string> tabs{"All", "Alpha", "Beta", "Gamma"};
    const std::string longText(kLongDescription);

    SECTION("in a narrow browser the long description takes three lines and ends with an ellipsis") {
        Krate::Plugins::PresetBrowserView view(VSTGUI::CRect(0, 0, 520, 600), &manager, tabs);
        view.open("Alpha");
        view.onPresetSelected(0);
        auto* label = descriptionLabelOf(view);
        REQUIRE(label != nullptr);

        const auto lines = splitLines(view.descriptionTextForTest());
        CHECK(lines.size() == 3u);
        REQUIRE_FALSE(lines.empty());
        CHECK(endsWith(lines.back(), kEllipsis));
        for (const auto& line : lines) {
            CHECK(textWidth(*label, line) <= textAreaWidth(*label));
        }
        // The lines are the description's opening words, none split, and it stops at a word boundary.
        std::string shown;
        for (const auto& line : lines) {
            shown += (shown.empty() ? "" : " ") + line;
        }
        if (endsWith(shown, kEllipsis)) {
            shown.resize(shown.size() - std::strlen(kEllipsis));
        }
        CHECK(longText.rfind(shown, 0) == 0u);
        REQUIRE(shown.size() < longText.size());
        CHECK(longText[shown.size()] == ' ');
        // The full text, never shortened, is the tooltip.
        CHECK(tooltipOf(*label) == longText);
        view.close();
    }

    SECTION("in Disrumpo's 1000 px browser it fits: wrapped at spaces, nothing dropped, no ellipsis") {
        Krate::Plugins::PresetBrowserView view(VSTGUI::CRect(0, 0, 1000, 600), &manager, tabs);
        view.open("Alpha");
        view.onPresetSelected(0);
        auto* label = descriptionLabelOf(view);
        REQUIRE(label != nullptr);

        const auto lines = splitLines(view.descriptionTextForTest());
        CHECK(lines.size() >= 2u);
        CHECK(lines.size() <= 3u);
        std::string joined;
        for (const auto& line : lines) {
            CHECK(textWidth(*label, line) <= textAreaWidth(*label));
            CHECK_FALSE(endsWith(line, kEllipsis));
            joined += (joined.empty() ? "" : " ") + line;
        }
        CHECK(joined == longText);
        CHECK(tooltipOf(*label) == longText);
        view.close();
    }

    SECTION("a description that fits one line shows unchanged, with itself as the tooltip") {
        Krate::Plugins::PresetBrowserView view(VSTGUI::CRect(0, 0, 1000, 600), &manager, tabs);
        view.open("Beta");
        view.onPresetSelected(0);
        auto* label = descriptionLabelOf(view);
        REQUIRE(label != nullptr);
        CHECK(view.descriptionTextForTest() == kShortDescription);
        CHECK(tooltipOf(*label) == kShortDescription);
        view.close();
    }

    SECTION("a preset without one shows nothing and leaves no tooltip behind") {
        Krate::Plugins::PresetBrowserView view(VSTGUI::CRect(0, 0, 1000, 600), &manager, tabs);
        view.open("Alpha");
        view.onPresetSelected(0);
        auto* label = descriptionLabelOf(view);
        REQUIRE(label != nullptr);
        REQUIRE(tooltipOf(*label) == longText);
        view.onCategoryTabChanged(2);  // filter index 2 = tabLabels_[3] = Gamma
        view.onPresetSelected(0);      // the only Gamma row: Undescribed
        CHECK(view.descriptionTextForTest().empty());
        CHECK(tooltipOf(*label).empty());
        view.close();
    }

    SECTION("at every editor size that hosts the browser, three lines fit, the list sits above them and keeps rows, "
            "and the frame's tooltip hit test reaches the description") {
        // The frame sizes PresetBrowserView is built at (each plugin's editor.uidesc template; Disrumpo opens at its
        // 600 px base height): Iterum, Disrumpo, Ruinae, Innexus, Gradus, Membrum, Seraphis, Vorago, Profundum.
        const std::vector<VSTGUI::CRect> frames{
            {0, 0, 940, 608},  {0, 0, 1000, 600}, {0, 0, 1400, 800}, {0, 0, 800, 1035}, {0, 0, 1100, 816},
            {0, 0, 1280, 800}, {0, 0, 1000, 700}, {0, 0, 1100, 760}, {0, 0, 960, 600}};
        for (const auto& frameRect : frames) {
            CAPTURE(frameRect.getWidth(), frameRect.getHeight());
            Krate::Plugins::PresetBrowserView view(frameRect, &manager, tabs);
            view.open("Alpha");
            view.onPresetSelected(0);
            auto* label = descriptionLabelOf(view);
            auto* list = directChildOf<VSTGUI::CDataBrowser>(view);
            auto* button = directChildOf<Krate::Plugins::OutlineBrowserButton>(view);
            REQUIRE(label != nullptr);
            REQUIRE(list != nullptr);
            REQUIRE(button != nullptr);

            const VSTGUI::CRect area = label->getViewSize();
            CHECK(list->getViewSize().bottom <= area.top);
            CHECK(area.bottom <= button->getViewSize().top);
            CHECK(list->getViewSize().getHeight() >= 24.0 * 10);  // at least ten 24 px rows (PresetDataSource)

            const auto font = label->getFont()->getPlatformFont();
            REQUIRE(font != nullptr);
            const double lineHeight = font->getAscent() + font->getDescent() + font->getLeading();
            CHECK(3.0 * lineHeight <= area.getHeight() - (2.0 * label->getTextInset().y));

            // CFrame::checkMouseViews finds the view under the mouse with deep().mouseEnabled() and hands it to
            // CTooltipSupport: the description must be that view, or its tooltip never shows.
            CHECK(view.getViewAt(area.getCenter(), VSTGUI::GetViewOptions().deep().mouseEnabled()) == label);
            view.close();
        }
    }
}

// Visual check of the description area (hidden: [.][screenshots]; never a gate). Renders the browser offscreen at 2x
// with the two Disrumpo descriptions at Disrumpo's 1000 x 600 size, and the long one in a narrow 520 px browser where
// it is cut to three lines. Nothing asserts on pixels.
// By hand: shared_tests.exe "Krate_PresetBrowserDescriptionShots"  with KRATE_PRESET_SHOTS_DIR set
// (default <temp>/preset-browser-shots)
TEST_CASE("Krate_PresetBrowserDescriptionShots", "[.][screenshots]") {
#if SMTG_OS_WINDOWS
    REQUIRE(SUCCEEDED(::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)));
#endif
    Krate::TestSupport::ensureVstguiInitialized();
    fs::path dir = fs::temp_directory_path() / "preset-browser-shots";
    if (const char* env = std::getenv("KRATE_PRESET_SHOTS_DIR"); env != nullptr && *env != '\0') {
        dir = env;
    }
    const DescTestDirs dirs;
    auto manager = makeDescManager(dirs);
    REQUIRE(manager.savePreset("Envelope Follower", "Alpha", kLongDescription));
    REQUIRE(manager.savePreset("Init 1 Band", "Beta", kShortDescription));
    const std::vector<std::string> tabs{"All", "Alpha", "Beta", "Gamma"};

    const auto shoot = [&](const VSTGUI::CRect& size, const std::string& category, const std::string& file) {
        auto frame = VSTGUI::makeOwned<VSTGUI::CFrame>(size, nullptr);
        auto* view = new Krate::Plugins::PresetBrowserView(size, &manager, tabs);
        frame->addView(view);
        view->attached(frame);  // the frame is never opened (no window), so attach by hand for the data browser
        view->open(category);
        view->onPresetSelected(0);
        const std::size_t bytes = writeBrowserShot(*view, dir / file);
        std::cout << file << ": " << size.getWidth() << " x " << size.getHeight() << ", " << bytes << " bytes\n";
        CHECK(bytes > 0u);
        view->close();
        frame->removeView(view);
    };
    shoot(VSTGUI::CRect(0, 0, 1000, 600), "Alpha", "disrumpo-long.png");
    shoot(VSTGUI::CRect(0, 0, 1000, 600), "Beta", "disrumpo-short.png");
    shoot(VSTGUI::CRect(0, 0, 520, 600), "Alpha", "narrow-truncated.png");
    std::cout << "preset browser shots written to " << dir.string() << '\n';
}
