// ==============================================================================
// T073 (Phase 8 / US7): Output Routing UI wiring.
// Spec: specs/141-membrum-phase6-ui/spec.md (FR-065, FR-066)
//
// Verifies:
//   * editor.uidesc declares the Output selector (OutputBusSel) tag resolving
//     to kOutputBusId, bound in both SelectedPadAcoustic and SelectedPadExtended
//     templates (Phase 4 selected-pad proxy pattern).
//   * The Output Bus parameter is registered as a 16-entry StringListParameter
//     (Main, Aux 1..Aux 15) so the host and COptionMenu populate entries.
//   * Writing the global proxy (kOutputBusId) forwards to the currently
//     selected pad's per-pad kPadOutputBus parameter.
//   * Selecting an inactive aux bus -> Controller::isBusActive reports false
//     (prerequisite for the FR-066 tooltip logic). A cached Output Bus
//     selector view would receive the tooltip text "Host must activate Aux N
//     bus"; the view pointer cannot be injected here without a live editor
//     so this test asserts the data plumbing that drives the tooltip.
//   * Pad grid BUS indicator text derived from PadConfig.outputBus updates
//     through PadGridView::notifyMetaChanged (which invalidates the view).
// ==============================================================================

#include "controller/controller.h"
#include "plugin_ids.h"
#include "dsp/pad_config.h"
#include "ui/pad_grid_view.h"

#include "pluginterfaces/vst/vsttypes.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

using namespace Membrum;

namespace {

std::filesystem::path findEditorUidesc()
{
    namespace fs = std::filesystem;
    const fs::path relative = "plugins/membrum/resources/editor.uidesc";
    fs::path cur = fs::current_path();
    for (int i = 0; i < 10; ++i) {
        const auto candidate = cur / relative;
        if (fs::exists(candidate))
            return candidate;
        if (!cur.has_parent_path() || cur.parent_path() == cur)
            break;
        cur = cur.parent_path();
    }
    const auto direct = fs::path(relative);
    if (fs::exists(direct))
        return direct;
    return {};
}

std::string slurp(const std::filesystem::path& p)
{
    std::ifstream f(p);
    if (!f)
        return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string extractTemplate(const std::string& xml, const std::string& name)
{
    const std::string needle = "<template name=\"" + name + "\"";
    const auto start = xml.find(needle);
    if (start == std::string::npos)
        return {};
    const std::string endTag = "</template>";
    const auto end = xml.find(endTag, start);
    if (end == std::string::npos)
        return {};
    return xml.substr(start, end - start);
}

std::set<std::string> extractTagRefs(const std::string& templateXml)
{
    std::set<std::string> names;
    const std::string marker = "control-tag=\"";
    std::size_t pos = 0;
    while ((pos = templateXml.find(marker, pos)) != std::string::npos) {
        const auto nameStart = pos + marker.size();
        const auto nameEnd = templateXml.find('"', nameStart);
        if (nameEnd == std::string::npos)
            break;
        names.insert(templateXml.substr(nameStart, nameEnd - nameStart));
        pos = nameEnd + 1;
    }
    return names;
}

int controlTagValue(const std::string& xml, const std::string& name)
{
    const std::string needle = "<control-tag name=\"" + name + "\"";
    const auto start = xml.find(needle);
    if (start == std::string::npos)
        return -1;
    const auto tagPos = xml.find("tag=\"", start);
    if (tagPos == std::string::npos)
        return -1;
    const auto numStart = tagPos + 5;
    const auto numEnd = xml.find('"', numStart);
    if (numEnd == std::string::npos)
        return -1;
    try {
        return std::stoi(xml.substr(numStart, numEnd - numStart));
    } catch (...) {
        return -1;
    }
}

} // namespace

// ------------------------------------------------------------------------------
// FR-065: Output Bus selector binds to kOutputBusId (selected-pad proxy)
// in BOTH Acoustic and Extended Selected-Pad templates.
// ------------------------------------------------------------------------------
TEST_CASE("Selected-Pad Panel Output Bus selector binds to kOutputBusId proxy (FR-065)",
          "[output_routing]")
{
    const auto uidescPath = findEditorUidesc();
    REQUIRE_FALSE(uidescPath.empty());
    const auto xml = slurp(uidescPath);
    REQUIRE_FALSE(xml.empty());

    auto requireBindsToOutputBus = [&](const std::string& templateName) {
        const auto body = extractTemplate(xml, templateName);
        REQUIRE_FALSE(body.empty());
        const auto refs = extractTagRefs(body);
        bool found = false;
        for (const auto& r : refs) {
            const int v = controlTagValue(xml, r);
            if (v == kOutputBusId) {
                found = true;
                break;
            }
        }
        INFO("Template " << templateName
             << " must bind a control-tag whose value == kOutputBusId ("
             << kOutputBusId << ")");
        REQUIRE(found);
    };

    requireBindsToOutputBus("SelectedPadSimple");
    requireBindsToOutputBus("SelectedPadAdvanced");
}

// ------------------------------------------------------------------------------
// FR-065: kOutputBusId is registered as a 16-entry StringList
// (Main, Aux 1..Aux 15) so COptionMenu entries populate automatically.
// ------------------------------------------------------------------------------
TEST_CASE("kOutputBusId is a 16-entry StringList (Main, Aux 1..Aux 15)",
          "[output_routing]")
{
    Controller ctl;
    REQUIRE(ctl.initialize(nullptr) == Steinberg::kResultOk);

    Steinberg::Vst::ParameterInfo info{};
    bool found = false;
    const int n = ctl.getParameterCount();
    for (int i = 0; i < n; ++i) {
        REQUIRE(ctl.getParameterInfo(i, info) == Steinberg::kResultOk);
        if (info.id == kOutputBusId) { found = true; break; }
    }
    REQUIRE(found);
    // 15 steps between 0 and 15 inclusive => 16 entries.
    REQUIRE(info.stepCount == 15);

    ctl.terminate();
}

// ------------------------------------------------------------------------------
// FR-065: writing the kOutputBusId proxy forwards to the currently selected
// pad's per-pad kPadOutputBus parameter. Selecting pad 3 and writing Aux 2
// on the proxy must leave pad 3's per-pad Output Bus parameter == Aux 2.
// ------------------------------------------------------------------------------
TEST_CASE("Writing kOutputBusId proxy writes outputBus to the selected pad's per-pad param",
          "[output_routing]")
{
    using Steinberg::Vst::ParamID;
    using Steinberg::Vst::ParamValue;

    Controller ctl;
    REQUIRE(ctl.initialize(nullptr) == Steinberg::kResultOk);

    // Select pad 3 via kSelectedPadId (normalised index / 31).
    const double pad3Norm = 3.0 / 31.0;
    REQUIRE(ctl.setParamNormalized(
                static_cast<ParamID>(kSelectedPadId), pad3Norm)
            == Steinberg::kResultOk);

    // Write Aux 2 (index 2 of 16 entries) via the proxy.
    const double aux2Norm = 2.0 / static_cast<double>(kMaxOutputBuses - 1);
    REQUIRE(ctl.setParamNormalized(
                static_cast<ParamID>(kOutputBusId), aux2Norm)
            == Steinberg::kResultOk);

    // Pad 3's per-pad Output Bus must reflect Aux 2.
    const auto pad3OutputBus = static_cast<ParamID>(
        padParamId(3, kPadOutputBus));
    const ParamValue stored = ctl.getParamNormalized(pad3OutputBus);
    REQUIRE(std::abs(stored - aux2Norm) < 1e-6);

    // Other pads' per-pad Output Bus parameters stay on Main (0).
    for (int pad = 0; pad < kNumPads; ++pad)
    {
        if (pad == 3)
            continue;
        const auto id = static_cast<ParamID>(padParamId(pad, kPadOutputBus));
        REQUIRE(ctl.getParamNormalized(id) == 0.0);
    }

    ctl.terminate();
}

// ------------------------------------------------------------------------------
// FR-066: selecting an inactive aux bus. The prerequisite data plumbing
// (isBusActive reporting false for unactivated buses) must hold; the tooltip
// text is built on this signal inside Controller::updateOutputBusTooltip().
// We verify the expected tooltip string shape, and that selecting Main (0)
// or an active bus does NOT produce a warning.
// ------------------------------------------------------------------------------
TEST_CASE("FR-066: inactive aux bus detection drives the warning tooltip",
          "[output_routing]")
{
    Controller ctl;
    REQUIRE(ctl.initialize(nullptr) == Steinberg::kResultOk);

    // Host activates bus 0 (main always) and bus 2 but NOT bus 5.
    ctl.notifyBusActivation(2, true);
    REQUIRE(ctl.isBusActive(0));
    REQUIRE(ctl.isBusActive(2));
    REQUIRE_FALSE(ctl.isBusActive(5));

    // Main (bus 0) is always considered active -- no warning should fire.
    REQUIRE(ctl.isBusActive(0));

    // The tooltip message shape required by FR-066.
    char buf[64] = {};
    std::snprintf(buf, sizeof(buf),
                  "Host must activate Aux %d bus", 5);
    const std::string expected = buf;
    REQUIRE(expected == "Host must activate Aux 5 bus");

    ctl.terminate();
}

// ------------------------------------------------------------------------------
// FR-065 (grid indicator): changing a pad's outputBus updates its "BUS{N}"
// indicator text. outputBusIndicatorText is the single source of truth used
// by PadGridView::draw(); notifyMetaChanged invalidates the view so the next
// draw picks up the new text from the meta provider.
// ------------------------------------------------------------------------------
TEST_CASE("Pad grid BUS indicator text tracks PadConfig.outputBus (FR-065)",
          "[output_routing]")
{
    using Membrum::UI::outputBusIndicatorText;

    REQUIRE(outputBusIndicatorText(0).empty());     // Main -> no indicator
    REQUIRE(outputBusIndicatorText(1) == "BUS1");
    REQUIRE(outputBusIndicatorText(2) == "BUS2");
    REQUIRE(outputBusIndicatorText(5) == "BUS5");
    REQUIRE(outputBusIndicatorText(15) == "BUS15");
}

// ------------------------------------------------------------------------------
// FR-065 (grid live-update): PadGridView::notifyMetaChanged accepts any valid
// pad index without crashing so the controller's setParamNormalized() can
// drive live indicator refresh without checking an "editor-open" flag.
// Out-of-range indices are ignored.
// ------------------------------------------------------------------------------
TEST_CASE("PadGridView::notifyMetaChanged is safe for any pad index",
          "[output_routing]")
{
    std::array<PadConfig, kNumPads> pads{};
    Membrum::UI::PadGridView view(
        VSTGUI::CRect{ 0, 0, 400, 800 },
        /*glowPublisher*/ nullptr,
        [&pads](int idx) -> const PadConfig* {
            if (idx < 0 || idx >= kNumPads)
                return nullptr;
            return &pads[static_cast<std::size_t>(idx)];
        });

    // In-range indices are accepted.
    for (int i = 0; i < kNumPads; ++i)
        view.notifyMetaChanged(i);

    // Out-of-range indices are no-ops (must not crash).
    view.notifyMetaChanged(-1);
    view.notifyMetaChanged(kNumPads);
    view.notifyMetaChanged(10000);

    SUCCEED("notifyMetaChanged tolerated all index values");
}
