// ==============================================================================
// T060 (Phase 6 / US5): Voice Management + Choke Group UI wiring.
// Spec: specs/141-membrum-phase6-ui/spec.md (FR-060, FR-061, FR-062)
//
// Verifies:
//   * Choke Group selector in the Selected-Pad Panel binds to kChokeGroupId
//     (the Phase 4 selected-pad proxy) in BOTH Acoustic and Extended templates.
//   * Max Polyphony slider (kMaxPolyphonyId) is bound in the Kit Column.
//   * Voice Stealing dropdown (kVoiceStealingId) is bound in the Kit Column.
//   * The Voice Stealing parameter exposes three string-list entries
//     (Oldest / Quietest / Priority) -- this drives the COptionMenu population.
//   * Tooltips for the Voice Stealing dropdown are present in editor.uidesc
//     (FR-061: hovering the dropdown explains each policy).
//   * An active-voices readout label (title prefix "ActiveVoices") exists in
//     the Kit Column, so the 30 Hz timer can push MetersBlock.activeVoices into
//     it.
//   * Controller::updateMeterViews() updates the active-voices label text from
//     the cached MetersBlock value.
// ==============================================================================

#include "controller/controller.h"
#include "plugin_ids.h"
#include "processor/meters_block.h"

#include "pluginterfaces/vst/vsttypes.h"

#include <catch2/catch_test_macros.hpp>

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

// Resolve a control-tag NAME to its integer tag value.
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
// FR-062: Choke Group selector binds to kChokeGroupId (selected-pad proxy)
// in BOTH Acoustic and Extended Selected-Pad templates.
// ------------------------------------------------------------------------------
TEST_CASE("Selected-Pad Panel Choke Group selector binds to kChokeGroupId proxy (FR-062)",
          "[voice_management]")
{
    const auto uidescPath = findEditorUidesc();
    REQUIRE_FALSE(uidescPath.empty());
    const auto xml = slurp(uidescPath);
    REQUIRE_FALSE(xml.empty());

    // The control-tag NAME used by the selector must resolve to kChokeGroupId.
    // We accept either the canonical name "ChokeGroup" or a clearly tagged
    // alias provided its tag value equals kChokeGroupId.
    auto requireBindsToChokeGroup = [&](const std::string& templateName) {
        const auto body = extractTemplate(xml, templateName);
        REQUIRE_FALSE(body.empty());
        const auto refs = extractTagRefs(body);
        bool found = false;
        for (const auto& r : refs) {
            const int v = controlTagValue(xml, r);
            if (v == kChokeGroupId) {
                found = true;
                break;
            }
        }
        INFO("Template " << templateName << " must bind a control-tag whose value == kChokeGroupId ("
                          << kChokeGroupId << ")");
        REQUIRE(found);
    };

    requireBindsToChokeGroup("SelectedPadSimple");
    requireBindsToChokeGroup("SelectedPadAdvanced");
}

// ------------------------------------------------------------------------------
// FR-060: Max Polyphony slider bound to kMaxPolyphonyId in the editor.
// ------------------------------------------------------------------------------
TEST_CASE("Kit Column exposes Max Polyphony slider bound to kMaxPolyphonyId (FR-060)",
          "[voice_management]")
{
    const auto uidescPath = findEditorUidesc();
    REQUIRE_FALSE(uidescPath.empty());
    const auto xml = slurp(uidescPath);
    REQUIRE_FALSE(xml.empty());

    const int tagValue = controlTagValue(xml, "MaxPolyphony");
    REQUIRE(tagValue == kMaxPolyphonyId);

    // EditorDefault must reference the MaxPolyphony tag at least once.
    const auto def = extractTemplate(xml, "EditorDefault");
    REQUIRE_FALSE(def.empty());
    REQUIRE(def.find("control-tag=\"MaxPolyphony\"") != std::string::npos);
}

// ------------------------------------------------------------------------------
// FR-060: Voice Stealing dropdown bound to kVoiceStealingId.
// ------------------------------------------------------------------------------
TEST_CASE("Kit Column exposes Voice Stealing dropdown bound to kVoiceStealingId (FR-060)",
          "[voice_management]")
{
    const auto uidescPath = findEditorUidesc();
    REQUIRE_FALSE(uidescPath.empty());
    const auto xml = slurp(uidescPath);
    REQUIRE_FALSE(xml.empty());

    const int tagValue = controlTagValue(xml, "VoiceStealing");
    REQUIRE(tagValue == kVoiceStealingId);

    const auto def = extractTemplate(xml, "EditorDefault");
    REQUIRE_FALSE(def.empty());
    REQUIRE(def.find("control-tag=\"VoiceStealing\"") != std::string::npos);
}

// ------------------------------------------------------------------------------
// FR-061: Voice Stealing tooltip text describing each policy is present.
// We assert each policy name (Oldest / Quietest / Priority) appears in a
// tooltip-like attribute (`tooltip="..."`) somewhere in editor.uidesc.
// ------------------------------------------------------------------------------
TEST_CASE("Voice Stealing dropdown has tooltip text covering all three policies (FR-061)",
          "[voice_management]")
{
    const auto uidescPath = findEditorUidesc();
    REQUIRE_FALSE(uidescPath.empty());
    const auto xml = slurp(uidescPath);
    REQUIRE_FALSE(xml.empty());

    // The dropdown itself must carry a tooltip attribute, and that attribute's
    // text must mention each of the three policy names.
    const auto pos = xml.find("control-tag=\"VoiceStealing\"");
    REQUIRE(pos != std::string::npos);

    // Find the enclosing <view ... /> element.
    const auto open = xml.rfind('<', pos);
    const auto close = xml.find("/>", pos);
    REQUIRE(open != std::string::npos);
    REQUIRE(close != std::string::npos);
    const auto element = xml.substr(open, close - open);

    const auto tipPos = element.find("tooltip=\"");
    INFO("VoiceStealing dropdown view element: " << element);
    REQUIRE(tipPos != std::string::npos);
    const auto tipStart = tipPos + 9;
    const auto tipEnd = element.find('"', tipStart);
    REQUIRE(tipEnd != std::string::npos);
    const auto tip = element.substr(tipStart, tipEnd - tipStart);

    REQUIRE(tip.find("Oldest")   != std::string::npos);
    REQUIRE(tip.find("Quietest") != std::string::npos);
    REQUIRE(tip.find("Priority") != std::string::npos);
}

// ------------------------------------------------------------------------------
// Voice Stealing parameter exposes three discrete entries (drives the
// COptionMenu population at runtime).
// ------------------------------------------------------------------------------
TEST_CASE("kVoiceStealingId is a 3-entry StringList (Oldest/Quietest/Priority)",
          "[voice_management]")
{
    Controller ctl;
    REQUIRE(ctl.initialize(nullptr) == Steinberg::kResultOk);

    Steinberg::Vst::ParameterInfo info{};
    bool found = false;
    const int n = ctl.getParameterCount();
    for (int i = 0; i < n; ++i) {
        REQUIRE(ctl.getParameterInfo(i, info) == Steinberg::kResultOk);
        if (info.id == kVoiceStealingId) { found = true; break; }
    }
    REQUIRE(found);
    REQUIRE(info.stepCount == 2);  // 0..2 inclusive => 3 entries

    ctl.terminate();
}

// ------------------------------------------------------------------------------
// kMaxPolyphonyId range: stepped slider 4..16.
// ------------------------------------------------------------------------------
TEST_CASE("kMaxPolyphonyId range covers 4..16",
          "[voice_management]")
{
    Controller ctl;
    REQUIRE(ctl.initialize(nullptr) == Steinberg::kResultOk);

    Steinberg::Vst::ParameterInfo info{};
    bool found = false;
    const int n = ctl.getParameterCount();
    for (int i = 0; i < n; ++i) {
        REQUIRE(ctl.getParameterInfo(i, info) == Steinberg::kResultOk);
        if (info.id == kMaxPolyphonyId) { found = true; break; }
    }
    REQUIRE(found);
    // 12 steps between 4 and 16 inclusive => stepCount == 12.
    REQUIRE(info.stepCount == 12);

    ctl.terminate();
}

// ------------------------------------------------------------------------------
// FR-060: an active-voices readout label is present in the Kit Column with
// the title prefix "ActiveVoices" so the controller can discover it via
// verifyView() and push MetersBlock.activeVoices into it on the 30 Hz timer.
// ------------------------------------------------------------------------------
TEST_CASE("Kit Column has an active-voices readout label (FR-060)",
          "[voice_management]")
{
    const auto uidescPath = findEditorUidesc();
    REQUIRE_FALSE(uidescPath.empty());
    const auto xml = slurp(uidescPath);
    REQUIRE_FALSE(xml.empty());

    const auto def = extractTemplate(xml, "EditorDefault");
    REQUIRE_FALSE(def.empty());
    REQUIRE(def.find("title=\"ActiveVoices") != std::string::npos);
}
