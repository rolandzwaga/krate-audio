// ==============================================================================
// Membrum Update Check Test
// ==============================================================================
// The live update check every Krate plugin runs: the controller owns the shared
// UpdateChecker, editor.uidesc places one UpdateBanner (the EditorDefault
// template's last child, so it paints over everything) and one
// CheckForUpdatesButton, and createCustomView builds both shared views.

#include <catch2/catch_test_macros.hpp>
#include <editor_lifecycle_harness.h>

#include "controller/controller.h"
#include "ui/update_banner_view.h"

#include "vstgui/uidescription/uiattributes.h"

#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string readEditorUidesc()
{
    std::ifstream in(std::string(MEMBRUM_RESOURCES_DIR) + "/editor.uidesc");
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::size_t countOccurrences(const std::string& text, const std::string& needle)
{
    std::size_t count = 0;
    for (std::size_t pos = text.find(needle); pos != std::string::npos; pos = text.find(needle, pos + 1))
        ++count;
    return count;
}

} // namespace

TEST_CASE("Membrum uidesc places one update banner and one check button",
          "[membrum][controller][ui][update]")
{
    const std::string xml = readEditorUidesc();
    REQUIRE_FALSE(xml.empty());
    CHECK(countOccurrences(xml, "custom-view-name=\"UpdateBanner\"") == 1);
    CHECK(countOccurrences(xml, "custom-view-name=\"CheckForUpdatesButton\"") == 1);

    // The banner is the editor template's last view: nothing opens between it and </template>.
    const std::size_t editorStart = xml.find("<template name=\"EditorDefault\"");
    REQUIRE(editorStart != std::string::npos);
    const std::size_t editorEnd = xml.find("</template>", editorStart);
    REQUIRE(editorEnd != std::string::npos);
    const std::size_t banner = xml.find("custom-view-name=\"UpdateBanner\"", editorStart);
    REQUIRE(banner != std::string::npos);
    CHECK(banner < editorEnd);
    CHECK(xml.find("<view", banner) > editorEnd);
}

TEST_CASE("Membrum createCustomView builds the shared update views",
          "[membrum][controller][ui][update]")
{
    Krate::TestSupport::ensureVstguiInitialized();
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);

    VSTGUI::UIAttributes attrs;
    attrs.setAttribute("origin", "0, 0");
    attrs.setAttribute("size", "1280, 60");

    VSTGUI::CView* banner = controller.createCustomView("UpdateBanner", attrs, nullptr, nullptr);
    REQUIRE(banner != nullptr);
    CHECK(dynamic_cast<Krate::Plugins::UpdateBannerView*>(banner) != nullptr);
    banner->forget();

    VSTGUI::CView* button = controller.createCustomView("CheckForUpdatesButton", attrs, nullptr, nullptr);
    REQUIRE(button != nullptr);
    CHECK(dynamic_cast<Krate::Plugins::CheckForUpdatesButton*>(button) != nullptr);
    button->forget();

    controller.terminate();
}
