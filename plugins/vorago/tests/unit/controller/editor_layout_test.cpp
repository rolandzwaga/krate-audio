// ==============================================================================
// Vorago - editor layout, binding, page-switch and Gravity-display tests
// ==============================================================================
// Phase 13 (specs/vorago-phase13-ui), task T006. Test only: the implementation
// is T013 (VoragoPanelSubController), T014 (controller), T015 (editor.uidesc),
// T016 (entry.cpp) and T017 (lifecycle test). Red until those land.
//
// XML half (SC-002 .. SC-005) parses resources/editor.uidesc directly:
//   - Vorago_UidescBindsEverySurfaceId (SC-002): unreachableParams(xml, all
//     108 + kNumEcosystemRosterParams, {4, 5}) is empty; the control-tag="..."
//     multiset is exactly the 106 + kNumEcosystemRosterParams non-hidden IDs
//     (Phase 14 FR-073 added 901/902 on page 6), each once; 4 and 5 never
//     bound; every bound element has a non-empty tooltip (FR-006, FR-010,
//     FR-011).
//   - Vorago_Ecosystem_PageBindsRosterIds (SC-028): page 6 binds 901 and 902;
//     the unbound allowlist is exactly {4, 5}; both knobs lie inside the
//     1100 x 296 page and overlap no other view.
//   - Vorago_UidescTagTable (SC-003): every <control-tag> is a registered ID,
//     every referenced name is declared, the 14 Phase 11 names keep their values,
//     every name is the plugin_ids.h enumerator minus "k"/"Id" (FR-012, plan 7.1).
//   - Vorago_UidescViewClassRule (SC-004): C-4's per-view class rule (FR-013).
//   - Vorago_UidescLayout (SC-005): C-1 regions, macro/page geometry, FR-003
//     macro order, FR-055 page ID sets, FR-006 sibling labels, FR-005 whitelist.
//   The element scan is a small stack parser (<view, <template, </view>,
//   </template>, />) in the anonymous namespace below; each node's rect is
//   resolved to window coordinates by summing ancestor origins (the template
//   root is the window origin).
//
// Built-tree half (SC-017, SC-018 session arm, SC-022) opens a headless
// VST3Editor exactly as editor_lifecycle_test.cpp does (attached(nullptr,
// nativePlatformType()) builds the tree and fires didOpen; removed() fires
// willClose). The frame is never platform-attached, so getParentView() is not
// reliable here: the walk below tracks ancestors itself (window rect and
// effective visibility = every ancestor visible).
//
// Session-only (SC-018): CControl::beginEdit/endEdit reach
// EditController::beginEdit only through the control's LISTENER
// (ccontrol.cpp:186-197; VST3Editor::beginEdit/endEdit are no-ops,
// vst3editor.cpp:691-701), so driving the real built CSegmentButton and
// OutlineBrowserButton with their full begin/value/end sequences proves the
// sub-controller swallows the session tags.
//
// Gravity (SC-022, FR-080, FR-081): the NORMAL tooltip is the one the uidesc
// gives the MacroGravity knob (plan 5.6: kGravityTip is that same literal); the
// INERT tooltip's exact wording is not fixed by the spec, so this test requires
// it to differ from the normal one and to contain "inert" (case-insensitive).
// The label strings "Gravity" / "Gravity (inert)" ARE fixed (tasks.md T014).
//
// NEVER name a kPlatformType* constant here - always
// Krate::TestSupport::nativePlatformType() (lint-platform-type-literals.js).
// ==============================================================================

#include "test_helpers/editor_lifecycle_harness.h"
#include "test_helpers/uidesc_reachability.h"

#include "controller/controller.h"
#include "parameters/param_mapping.h"
#include "parameters/resonance_params.h"
#include "plugin_ids.h"
#include "ui/ecosystem_view.h"
#include "ui/panel_sub_controller.h"
#include "unit/param_table_expected.h"
#include "vorago_test_fixture.h"  // VoragoTest::kNumEcosystemRosterParams

#include "ui/arc_knob.h"
#include "ui/outline_button.h"
#include "ui/preset_browser_view.h"

#include "pluginterfaces/base/smartpointer.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "vstgui/lib/cbuttonstate.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/controls/csegmentbutton.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "vstgui/uidescription/viewcreator/viewcreator.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

// ==============================================================================
// Shared tables
// ==============================================================================

/// Every registered ID -> its expected <control-tag> name (the plugin_ids.h
/// enumerator minus "k" and "Id"; plan 7.1). Generated once from plugin_ids.h
/// by a Node one-liner; the enumerators themselves make the compiler check the
/// ID column.
struct IdName {
    Steinberg::Vst::ParamID id;
    const char* name;
};

// clang-format off
constexpr std::array<IdName, 108 + VoragoTest::kNumEcosystemRosterParams> kIdNames = {{
    {.id = ::Vorago::kMasterGainId, .name = "MasterGain"},
    {.id = ::Vorago::kPolyphonyId, .name = "Polyphony"},
    {.id = ::Vorago::kSeedId, .name = "Seed"},
    {.id = ::Vorago::kOutputSaturationId, .name = "OutputSaturation"},
    {.id = ::Vorago::kSustainPedalId, .name = "SustainPedal"},
    {.id = ::Vorago::kChannelPressureId, .name = "ChannelPressure"},
    {.id = ::Vorago::kMacroDarknessId, .name = "MacroDarkness"},
    {.id = ::Vorago::kMacroAgeId, .name = "MacroAge"},
    {.id = ::Vorago::kMacroDensityId, .name = "MacroDensity"},
    {.id = ::Vorago::kMacroMovementId, .name = "MacroMovement"},
    {.id = ::Vorago::kMacroGravityId, .name = "MacroGravity"},
    {.id = ::Vorago::kMacroEntropyId, .name = "MacroEntropy"},
    {.id = ::Vorago::kMacroPressureId, .name = "MacroPressure"},
    {.id = ::Vorago::kMacroWeightId, .name = "MacroWeight"},
    {.id = ::Vorago::kMacroFogId, .name = "MacroFog"},
    {.id = ::Vorago::kMacroLifeId, .name = "MacroLife"},
    {.id = ::Vorago::kMacroDepthId, .name = "MacroDepth"},
    {.id = ::Vorago::kMacroMassId, .name = "MacroMass"},
    {.id = ::Vorago::kCloudRichnessId, .name = "CloudRichness"},
    {.id = ::Vorago::kCloudTiltId, .name = "CloudTilt"},
    {.id = ::Vorago::kCloudMutationId, .name = "CloudMutation"},
    {.id = ::Vorago::kCloudInharmonicityId, .name = "CloudInharmonicity"},
    {.id = ::Vorago::kCloudDriftDepthId, .name = "CloudDriftDepth"},
    {.id = ::Vorago::kCloudStereoSpreadId, .name = "CloudStereoSpread"},
    {.id = ::Vorago::kCloudSpectralGravityId, .name = "CloudSpectralGravity"},
    {.id = ::Vorago::kNoiseLevelId, .name = "NoiseLevel"},
    {.id = ::Vorago::kNoiseWakeId, .name = "NoiseWake"},
    {.id = ::Vorago::kNoiseWanderRateId, .name = "NoiseWanderRate"},
    {.id = ::Vorago::kNoiseSlot0ModelId, .name = "NoiseSlot0Model"},
    {.id = ::Vorago::kNoiseSlot1ModelId, .name = "NoiseSlot1Model"},
    {.id = ::Vorago::kNoiseSlot2ModelId, .name = "NoiseSlot2Model"},
    {.id = ::Vorago::kNoiseSlot3ModelId, .name = "NoiseSlot3Model"},
    {.id = ::Vorago::kNoiseSlot0TypeId, .name = "NoiseSlot0Type"},
    {.id = ::Vorago::kNoiseSlot1TypeId, .name = "NoiseSlot1Type"},
    {.id = ::Vorago::kNoiseSlot2TypeId, .name = "NoiseSlot2Type"},
    {.id = ::Vorago::kNoiseSlot3TypeId, .name = "NoiseSlot3Type"},
    {.id = ::Vorago::kNoiseSlot0CombFundamentalId, .name = "NoiseSlot0CombFundamental"},
    {.id = ::Vorago::kNoiseSlot1CombFundamentalId, .name = "NoiseSlot1CombFundamental"},
    {.id = ::Vorago::kNoiseSlot2CombFundamentalId, .name = "NoiseSlot2CombFundamental"},
    {.id = ::Vorago::kNoiseSlot3CombFundamentalId, .name = "NoiseSlot3CombFundamental"},
    {.id = ::Vorago::kNoiseSlot0CombSpreadId, .name = "NoiseSlot0CombSpread"},
    {.id = ::Vorago::kNoiseSlot1CombSpreadId, .name = "NoiseSlot1CombSpread"},
    {.id = ::Vorago::kNoiseSlot2CombSpreadId, .name = "NoiseSlot2CombSpread"},
    {.id = ::Vorago::kNoiseSlot3CombSpreadId, .name = "NoiseSlot3CombSpread"},
    {.id = ::Vorago::kNoiseSlot0CombFeedbackId, .name = "NoiseSlot0CombFeedback"},
    {.id = ::Vorago::kNoiseSlot1CombFeedbackId, .name = "NoiseSlot1CombFeedback"},
    {.id = ::Vorago::kNoiseSlot2CombFeedbackId, .name = "NoiseSlot2CombFeedback"},
    {.id = ::Vorago::kNoiseSlot3CombFeedbackId, .name = "NoiseSlot3CombFeedback"},
    {.id = ::Vorago::kResonanceGravityId, .name = "ResonanceGravity"},
    {.id = ::Vorago::kResonanceMixId, .name = "ResonanceMix"},
    {.id = ::Vorago::kResonanceWanderRateId, .name = "ResonanceWanderRate"},
    {.id = ::Vorago::kResonanceAnchorModeId, .name = "ResonanceAnchorMode"},
    {.id = ::Vorago::kEcologyMixId, .name = "EcologyMix"},
    {.id = ::Vorago::kEcologyLoopGainId, .name = "EcologyLoopGain"},
    {.id = ::Vorago::kEcologyLoop0FilterModeId, .name = "EcologyLoop0FilterMode"},
    {.id = ::Vorago::kEcologyLoop1FilterModeId, .name = "EcologyLoop1FilterMode"},
    {.id = ::Vorago::kEcologyLoop2FilterModeId, .name = "EcologyLoop2FilterMode"},
    {.id = ::Vorago::kEcologyLoop3FilterModeId, .name = "EcologyLoop3FilterMode"},
    {.id = ::Vorago::kEcologyLoop4FilterModeId, .name = "EcologyLoop4FilterMode"},
    {.id = ::Vorago::kEcologyLoop5FilterModeId, .name = "EcologyLoop5FilterMode"},
    {.id = ::Vorago::kSubLevelOffsetId, .name = "SubLevelOffset"},
    {.id = ::Vorago::kSubTrackingId, .name = "SubTracking"},
    {.id = ::Vorago::kSubDiv2LevelId, .name = "SubDiv2Level"},
    {.id = ::Vorago::kSubDiv4LevelId, .name = "SubDiv4Level"},
    {.id = ::Vorago::kSubFifthBelowLevelId, .name = "SubFifthBelowLevel"},
    {.id = ::Vorago::kSmearAmountId, .name = "SmearAmount"},
    {.id = ::Vorago::kSmearDecoherenceId, .name = "SmearDecoherence"},
    {.id = ::Vorago::kSmearTiltId, .name = "SmearTilt"},
    {.id = ::Vorago::kEventsRateScaleId, .name = "EventsRateScale"},
    {.id = ::Vorago::kEcosystemDepthId, .name = "EcosystemDepth"},
    {.id = ::Vorago::kEcosystemSyncRateId, .name = "EcosystemSyncRate"},
    {.id = ::Vorago::kEcosystemSelfAffinityId, .name = "EcosystemSelfAffinity"},
    {.id = ::Vorago::kBodyBlendId, .name = "BodyBlend"},
    {.id = ::Vorago::kBodyDampingId, .name = "BodyDamping"},
    {.id = ::Vorago::kBodyResonanceId, .name = "BodyResonance"},
    {.id = ::Vorago::kBodyMixId, .name = "BodyMix"},
    {.id = ::Vorago::kBodyMaterialAId, .name = "BodyMaterialA"},
    {.id = ::Vorago::kBodyMaterialBId, .name = "BodyMaterialB"},
    {.id = ::Vorago::kSpaceSizeId, .name = "SpaceSize"},
    {.id = ::Vorago::kSpaceDarknessId, .name = "SpaceDarkness"},
    {.id = ::Vorago::kSpaceDecayId, .name = "SpaceDecay"},
    {.id = ::Vorago::kSpaceFogId, .name = "SpaceFog"},
    {.id = ::Vorago::kSpaceDamperDepthId, .name = "SpaceDamperDepth"},
    {.id = ::Vorago::kSpaceMixId, .name = "SpaceMix"},
    {.id = ::Vorago::kSpaceWidthId, .name = "SpaceWidth"},
    {.id = ::Vorago::kSpaceDensityId, .name = "SpaceDensity"},
    {.id = ::Vorago::kSpaceDimensionalityId, .name = "SpaceDimensionality"},
    {.id = ::Vorago::kSpaceBreathId, .name = "SpaceBreath"},
    {.id = ::Vorago::kSpaceEarlySizeId, .name = "SpaceEarlySize"},
    {.id = ::Vorago::kSpaceEarlyLevelId, .name = "SpaceEarlyLevel"},
    {.id = ::Vorago::kSpaceEarlyAbsorptionId, .name = "SpaceEarlyAbsorption"},
    {.id = ::Vorago::kSpaceEarlySendId, .name = "SpaceEarlySend"},
    {.id = ::Vorago::kSpaceDamperRateId, .name = "SpaceDamperRate"},
    {.id = ::Vorago::kSpaceFreezeId, .name = "SpaceFreeze"},
    {.id = ::Vorago::kEnvelopeModeId, .name = "EnvelopeMode"},
    {.id = ::Vorago::kEnvelopeStage0TimeId, .name = "EnvelopeStage0Time"},
    {.id = ::Vorago::kEnvelopeStage1TimeId, .name = "EnvelopeStage1Time"},
    {.id = ::Vorago::kEnvelopeStage2TimeId, .name = "EnvelopeStage2Time"},
    {.id = ::Vorago::kEnvelopeStage3TimeId, .name = "EnvelopeStage3Time"},
    {.id = ::Vorago::kEnvelopeReleaseId, .name = "EnvelopeRelease"},
    {.id = ::Vorago::kEnvelopeGrowthDurationId, .name = "EnvelopeGrowthDuration"},
    {.id = ::Vorago::kBloomDepthId, .name = "BloomDepth"},
    {.id = ::Vorago::kBloomSpawnRateId, .name = "BloomSpawnRate"},
    {.id = ::Vorago::kGhostPeakLevelId, .name = "GhostPeakLevel"},
    {.id = ::Vorago::kGhostBlurId, .name = "GhostBlur"},
    {.id = ::Vorago::kGhostReverseProbabilityId, .name = "GhostReverseProbability"},
    {.id = ::Vorago::kGhostEventTriggersId, .name = "GhostEventTriggers"},
    {.id = ::Vorago::kLifeBreathingDepthId, .name = "LifeBreathingDepth"},
    {.id = ::Vorago::kLifeBreathingIrregularityId, .name = "LifeBreathingIrregularity"},
    {.id = ::Vorago::kLifeTidalDepthId, .name = "LifeTidalDepth"},
}};
// clang-format on

/// Spec C-4 page table (0-based page index = uidesc-label "page-N").
std::vector<std::set<int>> expectedPageIds() {
    auto range = [](std::set<int>& s, int lo, int hi) {
        for (int id = lo; id <= hi; ++id) {
            s.insert(id);
        }
    };
    std::vector<std::set<int>> pages(7);
    // page-0 Cloud
    range(pages[0], 200, 206);
    range(pages[0], 1300, 1301);
    // page-1 Noise
    range(pages[1], 300, 302);
    range(pages[1], 310, 313);
    range(pages[1], 320, 323);
    range(pages[1], 330, 333);
    range(pages[1], 340, 343);
    range(pages[1], 350, 353);
    // page-2 Resonance + Ecology
    range(pages[2], 400, 403);
    range(pages[2], 500, 501);
    range(pages[2], 510, 515);
    // page-3 Body + Envelope
    range(pages[3], 1000, 1005);
    range(pages[3], 1200, 1206);
    // page-4 Sub / Smear
    range(pages[4], 600, 601);
    range(pages[4], 610, 612);
    range(pages[4], 700, 702);
    // page-5 Space
    range(pages[5], 1100, 1115);
    // page-6 Life (Events, Ecosystem, Ghost, Life)
    pages[6].insert(800);
    pages[6].insert(900);
    pages[6].insert(901);  // Phase 14 FR-073: Ecosystem Sync
    pages[6].insert(902);  // Phase 14 FR-073: Ecosystem Self Affinity
    range(pages[6], 1400, 1403);
    range(pages[6], 1500, 1502);
    return pages;
}

[[nodiscard]] const VoragoTest::ExpectedParamRow* findExpected(int id) {
    for (const auto& row : VoragoTest::kExpectedParams) {
        if (std::cmp_equal(row.id, id)) {
            return &row;
        }
    }
    return nullptr;
}

[[nodiscard]] bool isHiddenRow(const VoragoTest::ExpectedParamRow& row) {
    return (row.flags & Steinberg::Vst::ParameterInfo::kIsHidden) != 0;
}

[[nodiscard]] bool isListRow(const VoragoTest::ExpectedParamRow& row) {
    return (row.flags & Steinberg::Vst::ParameterInfo::kIsList) != 0;
}

[[nodiscard]] std::string readUidesc() {
    const std::string path = std::string(VORAGO_RESOURCES_DIR) + "/editor.uidesc";
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file.good());
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

[[nodiscard]] std::string toLower(std::string s) {
    for (auto& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

// ==============================================================================
// Stack-based uidesc element scan (SC-004, SC-005)
// ==============================================================================

struct Rect {
    double left = 0.0;
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;

    [[nodiscard]] double width() const { return right - left; }
    [[nodiscard]] double height() const { return bottom - top; }
    [[nodiscard]] bool contains(const Rect& o) const {
        return o.left >= left && o.top >= top && o.right <= right && o.bottom <= bottom;
    }
    [[nodiscard]] bool operator==(const Rect& o) const {
        return left == o.left && top == o.top && right == o.right && bottom == o.bottom;
    }
};

// NOLINTNEXTLINE(bugprone-exception-escape) -- implicit special members of std::map/std::string members; test-only value type
struct XmlNode {
    std::string element;  ///< "view" or "template"
    std::map<std::string, std::string> attrs;
    int parent = -1;
    std::vector<int> children;
    Rect window;  ///< resolved window coordinates

    [[nodiscard]] bool has(const std::string& key) const { return attrs.contains(key); }
    [[nodiscard]] std::string get(const std::string& key) const {
        const auto it = attrs.find(key);
        return it == attrs.end() ? std::string{} : it->second;
    }
};

struct XmlScan {
    std::vector<XmlNode> nodes;
    bool wellFormed = true;
};

/// "x, y" -> {x, y}. strtod, not sscanf (MSVC C4996).
[[nodiscard]] bool parsePoint(const std::string& s, double& x, double& y) {
    const char* p = s.c_str();
    char* end = nullptr;
    x = std::strtod(p, &end);
    if (end == p) {
        return false;
    }
    p = end;
    while (*p == ' ' || *p == ',') {
        ++p;
    }
    y = std::strtod(p, &end);
    return end != p;
}

/// The five predefined XML entities (UIAttributes values arrive decoded).
[[nodiscard]] std::string decodeEntities(const std::string& in) {
    static const std::array<std::pair<const char*, char>, 5> kEntities = {{
        {"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\x27'},
    }};
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size();) {
        bool replaced = false;
        if (in[i] == '&') {
            for (const auto& [entity, ch] : kEntities) {
                const std::size_t len = std::strlen(entity);
                if (in.compare(i, len, entity) == 0) {
                    out.push_back(ch);
                    i += len;
                    replaced = true;
                    break;
                }
            }
        }
        if (!replaced) {
            out.push_back(in[i]);
            ++i;
        }
    }
    return out;
}

[[nodiscard]] bool isNameChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_' || c == ':';
}

XmlScan scanUidesc(const std::string& xml) {
    XmlScan scan;
    std::vector<int> stack;
    std::size_t pos = 0;
    while ((pos = xml.find('<', pos)) != std::string::npos) {
        if (xml.compare(pos, 4, "<!--") == 0) {
            const std::size_t e = xml.find("-->", pos);
            if (e == std::string::npos) {
                scan.wellFormed = false;
                break;
            }
            pos = e + 3;
            continue;
        }
        if (xml.compare(pos, 2, "<?") == 0) {
            const std::size_t e = xml.find("?>", pos);
            if (e == std::string::npos) {
                scan.wellFormed = false;
                break;
            }
            pos = e + 2;
            continue;
        }
        if (xml.compare(pos, 2, "</") == 0) {
            const std::size_t e = xml.find('>', pos);
            if (e == std::string::npos) {
                scan.wellFormed = false;
                break;
            }
            std::size_t n = pos + 2;
            std::string name;
            while (n < e && isNameChar(xml[n])) {
                name.push_back(xml[n++]);
            }
            if (name == "view" || name == "template") {
                if (stack.empty() || scan.nodes[static_cast<std::size_t>(stack.back())].element != name) {
                    scan.wellFormed = false;
                } else {
                    stack.pop_back();
                }
            }
            pos = e + 1;
            continue;
        }

        // Opening (or self-closing) element: name, then quoted attributes up to
        // an UNQUOTED '>' (a tooltip may legitimately contain '>').
        std::size_t p = pos + 1;
        std::string name;
        while (p < xml.size() && isNameChar(xml[p])) {
            name.push_back(xml[p++]);
        }
        std::map<std::string, std::string> attrs;
        bool selfClosing = false;
        bool closed = false;
        while (p < xml.size()) {
            const char c = xml[p];
            if (c == '>') {
                closed = true;
                ++p;
                break;
            }
            if (c == '/') {
                selfClosing = true;
                ++p;
                continue;
            }
            if (std::isspace(static_cast<unsigned char>(c)) != 0) {
                ++p;
                continue;
            }
            std::string key;
            while (p < xml.size() && isNameChar(xml[p])) {
                key.push_back(xml[p++]);
            }
            if (key.empty()) {
                ++p;  // stray character; skip it
                continue;
            }
            while (p < xml.size() && std::isspace(static_cast<unsigned char>(xml[p])) != 0) {
                ++p;
            }
            std::string value;
            if (p < xml.size() && xml[p] == '=') {
                ++p;
                while (p < xml.size() && std::isspace(static_cast<unsigned char>(xml[p])) != 0) {
                    ++p;
                }
                if (p < xml.size() && (xml[p] == '"' || xml[p] == '\'')) {
                    const char q = xml[p++];
                    const std::size_t e = xml.find(q, p);
                    if (e == std::string::npos) {
                        scan.wellFormed = false;
                        p = xml.size();
                        break;
                    }
                    value = decodeEntities(xml.substr(p, e - p));
                    p = e + 1;
                }
            }
            attrs[key] = value;
            selfClosing = false;  // a '/' only counts right before '>'
        }
        if (!closed) {
            scan.wellFormed = false;
            break;
        }
        if (name == "view" || name == "template") {
            XmlNode node;
            node.element = name;
            node.attrs = std::move(attrs);
            node.parent = stack.empty() ? -1 : stack.back();
            const int index = static_cast<int>(scan.nodes.size());
            scan.nodes.push_back(std::move(node));
            if (scan.nodes.back().parent >= 0) {
                scan.nodes[static_cast<std::size_t>(scan.nodes.back().parent)].children.push_back(index);
            }
            if (!selfClosing) {
                stack.push_back(index);
            }
        }
        pos = p;
    }
    if (!stack.empty()) {
        scan.wellFormed = false;
    }

    // Resolve window rects: a template root is the window origin; a view is its
    // parent's window top-left plus its own origin. Parents precede children.
    for (auto& node : scan.nodes) {
        double w = 0.0;
        double h = 0.0;
        (void)parsePoint(node.get("size"), w, h);
        double ox = 0.0;
        double oy = 0.0;
        if (node.parent >= 0) {
            (void)parsePoint(node.get("origin"), ox, oy);
            const Rect& pr = scan.nodes[static_cast<std::size_t>(node.parent)].window;
            ox += pr.left;
            oy += pr.top;
        }
        node.window = Rect{.left = ox, .top = oy, .right = ox + w, .bottom = oy + h};
    }
    return scan;
}

[[nodiscard]] int findTemplate(const XmlScan& scan, const std::string& name) {
    for (std::size_t i = 0; i < scan.nodes.size(); ++i) {
        if (scan.nodes[i].element == "template" && scan.nodes[i].get("name") == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

[[nodiscard]] bool isDescendantOf(const XmlScan& scan, int node, int ancestor) {
    int p = scan.nodes[static_cast<std::size_t>(node)].parent;
    while (p >= 0) {
        if (p == ancestor) {
            return true;
        }
        p = scan.nodes[static_cast<std::size_t>(p)].parent;
    }
    return false;
}

/// Every node carrying uidesc-label == label.
[[nodiscard]] std::vector<int> findLabelled(const XmlScan& scan, const std::string& label) {
    std::vector<int> out;
    for (std::size_t i = 0; i < scan.nodes.size(); ++i) {
        if (scan.nodes[i].get("uidesc-label") == label) {
            out.push_back(static_cast<int>(i));
        }
    }
    return out;
}

/// Bound view nodes: (node index, resolved ParamID).
[[nodiscard]] std::vector<std::pair<int, int>> boundNodes(const XmlScan& scan,
                                                           const std::map<std::string, int>& tagMap) {
    std::vector<std::pair<int, int>> out;
    for (std::size_t i = 0; i < scan.nodes.size(); ++i) {
        const auto& n = scan.nodes[i];
        if (n.element != "view" || !n.has("control-tag")) {
            continue;
        }
        const auto it = tagMap.find(n.get("control-tag"));
        out.emplace_back(static_cast<int>(i), it == tagMap.end() ? -1 : it->second);
    }
    return out;
}

// ==============================================================================
// Built-tree helpers (SC-017, SC-018, SC-022)
// ==============================================================================

/// Reads a string CView attribute (e.g. the 'uilb' uidesc-label or 'cvtt' tooltip).
[[nodiscard]] std::string viewStringAttribute(const VSTGUI::CView* view, VSTGUI::CViewAttributeID id) {
    std::uint32_t size = 0;
    if (view == nullptr || !view->getAttributeSize(id, size) || size == 0) {
        return {};
    }
    std::vector<char> buf(size + 1, '\0');
    std::uint32_t outSize = 0;
    if (!view->getAttribute(id, size, buf.data(), outSize)) {
        return {};
    }
    return {buf.data()};
}

[[nodiscard]] std::string uidescLabelOf(const VSTGUI::CView* view) {
    return viewStringAttribute(view, VSTGUI::UIViewCreator::ViewCreator::labelAttrID);
}

[[nodiscard]] std::string tooltipOf(const VSTGUI::CView* view) {
    return viewStringAttribute(view, VSTGUI::kCViewTooltipAttribute);
}

struct BuiltView {
    VSTGUI::CView* view = nullptr;
    VSTGUI::CRect window;
    bool effectiveVisible = false;
    std::string label;
};

void walkBuilt(VSTGUI::CViewContainer* container, VSTGUI::CPoint offset, bool visible,
               std::vector<BuiltView>& out) {
    if (container == nullptr) {
        return;
    }
    const std::uint32_t count = container->getNbViews();
    for (std::uint32_t i = 0; i < count; ++i) {
        VSTGUI::CView* v = container->getView(i);
        if (v == nullptr) {
            continue;
        }
        VSTGUI::CRect r = v->getViewSize();
        r.offset(offset.x, offset.y);
        const bool vis = visible && v->isVisible();
        out.push_back(BuiltView{.view = v, .window = r, .effectiveVisible = vis, .label = uidescLabelOf(v)});
        // The didOpen overlay is not part of the uidesc surface and holds its own
        // OutlineBrowserButtons (preset_browser_view.cpp:503-520): never descend.
        if (dynamic_cast<Krate::Plugins::PresetBrowserView*>(v) != nullptr) {
            continue;
        }
        if (auto* child = dynamic_cast<VSTGUI::CViewContainer*>(v)) {
            walkBuilt(child, r.getTopLeft(), vis, out);
        }
    }
}

[[nodiscard]] std::vector<BuiltView> walkEditor(VSTGUI::VST3Editor* editor) {
    std::vector<BuiltView> out;
    REQUIRE(editor->getFrame() != nullptr);
    walkBuilt(editor->getFrame(), VSTGUI::CPoint(0, 0), true, out);
    return out;
}

/// RAII headless editor (the editor_lifecycle_test.cpp open idiom); closes on
/// scope exit so a failing REQUIRE never leaks an open editor.
class EditorSession {
public:
    explicit EditorSession(::Vorago::Controller& controller)
        : editor_(makeEditor(controller)),
          view_(editor_),
          attached_(view_->attached(nullptr, Krate::TestSupport::nativePlatformType()) ==
                    Steinberg::kResultTrue) {}
    ~EditorSession() { close(); }
    EditorSession(const EditorSession&) = delete;
    EditorSession& operator=(const EditorSession&) = delete;

    [[nodiscard]] bool attached() const { return attached_ && editor_->getFrame() != nullptr; }
    [[nodiscard]] VSTGUI::VST3Editor* editor() const { return editor_; }

    void close() {
        if (view_ != nullptr) {
            view_->removed();
            view_->release();
            view_ = nullptr;
            editor_ = nullptr;
        }
    }

private:
    [[nodiscard]] static VSTGUI::VST3Editor* makeEditor(::Vorago::Controller& controller) {
        Krate::TestSupport::ensureVstguiInitialized();
        const std::string uidescPath = std::string(VORAGO_RESOURCES_DIR) + "/editor.uidesc";
        return new VSTGUI::VST3Editor(&controller, "editor", uidescPath.c_str());
    }

    VSTGUI::VST3Editor* editor_ = nullptr;
    Steinberg::IPlugView* view_ = nullptr;
    bool attached_ = false;
};

/// The built surface the page/Gravity cases look at.
// NOLINTNEXTLINE(bugprone-exception-escape) -- implicit special members of std::map/std::string members; test-only value type
struct Surface {
    std::array<VSTGUI::CViewContainer*, 7> pages{};
    std::array<BuiltView, 7> pageRecs{};
    VSTGUI::CSegmentButton* segment = nullptr;
    Krate::Plugins::OutlineBrowserButton* presetButton = nullptr;
    BuiltView ecosystem;
    std::map<int, BuiltView> macros;  ///< 100..111
    VSTGUI::CControl* gravityKnob = nullptr;
    VSTGUI::CTextLabel* gravityLabel = nullptr;
    int segmentCount = 0;
    int presetButtonCount = 0;
    int ecosystemCount = 0;
    int gravityLabelCount = 0;
};

[[nodiscard]] Surface collectSurface(VSTGUI::VST3Editor* editor) {
    Surface s;
    const auto recs = walkEditor(editor);
    for (const auto& r : recs) {
        for (int k = 0; k < 7; ++k) {
            if (r.label == "page-" + std::to_string(k)) {
                if (auto* c = dynamic_cast<VSTGUI::CViewContainer*>(r.view)) {
                    s.pages[static_cast<std::size_t>(k)] = c;
                    s.pageRecs[static_cast<std::size_t>(k)] = r;
                }
            }
        }
        if (auto* seg = dynamic_cast<VSTGUI::CSegmentButton*>(r.view)) {
            s.segment = seg;
            ++s.segmentCount;
        }
        if (auto* btn = dynamic_cast<Krate::Plugins::OutlineBrowserButton*>(r.view)) {
            s.presetButton = btn;
            ++s.presetButtonCount;
        }
        if (dynamic_cast<::Vorago::UI::EcosystemView*>(r.view) != nullptr) {
            s.ecosystem = r;
            ++s.ecosystemCount;
        }
        if (auto* control = dynamic_cast<VSTGUI::CControl*>(r.view)) {
            const std::int32_t tag = control->getTag();
            if (tag >= static_cast<std::int32_t>(::Vorago::kMacroDarknessId) &&
                tag <= static_cast<std::int32_t>(::Vorago::kMacroMassId)) {
                s.macros[tag] = r;
                if (tag == static_cast<std::int32_t>(::Vorago::kMacroGravityId)) {
                    s.gravityKnob = control;
                }
            }
        }
        if (r.label == "macro-gravity-label") {
            if (auto* label = dynamic_cast<VSTGUI::CTextLabel*>(r.view)) {
                s.gravityLabel = label;
                ++s.gravityLabelCount;
            }
        }
    }
    return s;
}

/// Exactly page k (and no other page container) is visible.
[[nodiscard]] bool onlyPageVisible(const Surface& s, int k) {
    for (int i = 0; i < 7; ++i) {
        const auto* page = s.pages[static_cast<std::size_t>(i)];
        if (page == nullptr) {
            return false;
        }
        if (page->isVisible() != (i == k)) {
            return false;
        }
    }
    return true;
}

// ------------------------------------------------------------------------------
// Recording IComponentHandler (plugins/seraphis/tests/integration/
// preset_load_test.cpp shape). Stack-owned: ref counts are inert.
// ------------------------------------------------------------------------------
class RecordingHandler final : public Steinberg::Vst::IComponentHandler {
public:
    int begins = 0;
    int performs = 0;
    int ends = 0;

    [[nodiscard]] int total() const noexcept { return begins + performs + ends; }

    Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID /*id*/) override {
        ++begins;
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID /*id*/,
                                              Steinberg::Vst::ParamValue /*value*/) override {
        ++performs;
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID /*id*/) override {
        ++ends;
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32 /*flags*/) override {
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override {
        if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IComponentHandler::iid) ||
            Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid)) {
            *obj = static_cast<Steinberg::Vst::IComponentHandler*>(this);
            return Steinberg::kResultOk;
        }
        *obj = nullptr;
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }
};

/// id -> normalized value for every registered parameter.
[[nodiscard]] std::map<Steinberg::Vst::ParamID, double> snapshotParams(::Vorago::Controller& c) {
    std::map<Steinberg::Vst::ParamID, double> out;
    const Steinberg::int32 n = c.getParameterCount();
    for (Steinberg::int32 i = 0; i < n; ++i) {
        Steinberg::Vst::ParameterInfo info{};
        REQUIRE(c.getParameterInfo(i, info) == Steinberg::kResultOk);
        out[info.id] = c.getParamNormalized(info.id);
    }
    return out;
}

/// The uidesc tooltip of the view bound to control-tag "MacroGravity" (plan 5.6:
/// the controller's kGravityTip is the same literal).
[[nodiscard]] std::string uidescGravityTooltip() {
    const std::string xml = readUidesc();
    const auto scan = scanUidesc(xml);
    std::string tip;
    int found = 0;
    for (const auto& n : scan.nodes) {
        if (n.element == "view" && n.get("control-tag") == "MacroGravity") {
            tip = n.get("tooltip");
            ++found;
        }
    }
    REQUIRE(found == 1);
    REQUIRE_FALSE(tip.empty());
    return tip;
}

}  // namespace

// ==============================================================================
// SC-002 - binding completeness
// ==============================================================================
TEST_CASE("Vorago_UidescBindsEverySurfaceId", "[vorago][controller][ui]") {
    const std::string xml = readUidesc();

    std::vector<int> all108;
    std::set<int> nonHidden;
    for (const auto& row : VoragoTest::kExpectedParams) {
        all108.push_back(static_cast<int>(row.id));
        if (!isHiddenRow(row)) {
            nonHidden.insert(static_cast<int>(row.id));
        }
    }
    REQUIRE(all108.size() == 108u + VoragoTest::kNumEcosystemRosterParams);
    REQUIRE(nonHidden.size() == 106u + VoragoTest::kNumEcosystemRosterParams);
    REQUIRE(!nonHidden.contains(static_cast<int>(::Vorago::kSustainPedalId)));
    REQUIRE(!nonHidden.contains(static_cast<int>(::Vorago::kChannelPressureId)));

    // FR-010 / FR-011: every registered ID reachable except the allowlist {4, 5}.
    const auto missing = Krate::Test::unreachableParams(
        xml, all108,
        {static_cast<int>(::Vorago::kSustainPedalId), static_cast<int>(::Vorago::kChannelPressureId)});
    CAPTURE(missing);
    REQUIRE(missing.empty());

    // The multiset of control-tag="..." attribute values, mapped to IDs.
    const auto tagMap = Krate::Test::extractControlTagMap(xml);
    std::multiset<int> boundIds;
    std::vector<std::string> unresolved;
    {
        const std::string marker = "control-tag=\"";
        std::size_t pos = 0;
        while ((pos = xml.find(marker, pos)) != std::string::npos) {
            const std::size_t ns = pos + marker.size();
            const std::size_t ne = xml.find('"', ns);
            REQUIRE(ne != std::string::npos);
            const std::string name = xml.substr(ns, ne - ns);
            const auto it = tagMap.find(name);
            if (it == tagMap.end()) {
                unresolved.push_back(name);
            } else {
                boundIds.insert(it->second);
            }
            pos = ne + 1;
        }
    }
    CAPTURE(unresolved);
    REQUIRE(unresolved.empty());
    REQUIRE(boundIds.size() == 106u + VoragoTest::kNumEcosystemRosterParams);
    for (const int id : nonHidden) {
        CAPTURE(id);
        REQUIRE(boundIds.count(id) == 1u);
    }
    REQUIRE(!boundIds.contains(static_cast<int>(::Vorago::kSustainPedalId)));
    REQUIRE(!boundIds.contains(static_cast<int>(::Vorago::kChannelPressureId)));

    // FR-006: every element carrying a control-tag carries a non-empty tooltip.
    // The element scan must see every control-tag attribute the raw scan saw.
    const auto scan = scanUidesc(xml);
    REQUIRE(scan.wellFormed);
    std::size_t taggedElements = 0;
    for (const auto& n : scan.nodes) {
        if (!n.has("control-tag")) {
            continue;
        }
        ++taggedElements;
        CAPTURE(n.get("control-tag"));
        REQUIRE_FALSE(n.get("tooltip").empty());
    }
    REQUIRE(taggedElements == boundIds.size());
}

// ==============================================================================
// SC-003 - tag table integrity
// ==============================================================================
TEST_CASE("Vorago_UidescTagTable", "[vorago][controller][ui]") {
    const std::string xml = readUidesc();
    const auto tagMap = Krate::Test::extractControlTagMap(xml);
    const auto referenced = Krate::Test::extractReferencedTagNames(xml);

    std::map<int, std::string> expectedName;
    for (const auto& e : kIdNames) {
        expectedName[static_cast<int>(e.id)] = e.name;
    }
    REQUIRE(expectedName.size() == 108u + VoragoTest::kNumEcosystemRosterParams);

    // Every <control-tag> value is a registered ID and its name is the
    // plugin_ids.h enumerator minus "k"/"Id".
    std::set<int> declaredValues;
    for (const auto& [name, value] : tagMap) {
        CAPTURE(name, value);
        REQUIRE(findExpected(value) != nullptr);
        REQUIRE(expectedName.count(value) == 1u);
        REQUIRE(name == expectedName[value]);
        REQUIRE(declaredValues.insert(value).second);  // one name per ID
    }
    // Plan 7.1: 106 tags (+ the Phase 14 roster 901/902), none for the hidden
    // performance controllers.
    REQUIRE(tagMap.size() == 106u + VoragoTest::kNumEcosystemRosterParams);
    REQUIRE(!declaredValues.contains(static_cast<int>(::Vorago::kSustainPedalId)));
    REQUIRE(!declaredValues.contains(static_cast<int>(::Vorago::kChannelPressureId)));

    // Every control-tag used by a view is declared.
    for (const auto& name : referenced) {
        CAPTURE(name);
        REQUIRE(tagMap.count(name) == 1u);
    }

    // FR-012: the 14 Phase 11 names keep their values.
    const std::array<std::pair<const char*, int>, 14> phase11 = {{
        {"MasterGain", 0},
        {"Polyphony", 1},
        {"MacroDarkness", 100},
        {"MacroAge", 101},
        {"MacroDensity", 102},
        {"MacroMovement", 103},
        {"MacroGravity", 104},
        {"MacroEntropy", 105},
        {"MacroPressure", 106},
        {"MacroWeight", 107},
        {"MacroFog", 108},
        {"MacroLife", 109},
        {"MacroDepth", 110},
        {"MacroMass", 111},
    }};
    for (const auto& [name, value] : phase11) {
        CAPTURE(name);
        const auto it = tagMap.find(name);
        REQUIRE(it != tagMap.end());
        REQUIRE(it->second == value);
    }
}

// ==============================================================================
// SC-004 - per-view class rule (C-4)
// ==============================================================================
TEST_CASE("Vorago_UidescViewClassRule", "[vorago][controller][ui]") {
    const std::string xml = readUidesc();
    const auto tagMap = Krate::Test::extractControlTagMap(xml);
    const auto scan = scanUidesc(xml);
    REQUIRE(scan.wellFormed);

    std::size_t continuousRows = 0;
    std::size_t listRows = 0;
    for (const auto& row : VoragoTest::kExpectedParams) {
        if (isHiddenRow(row)) {
            continue;
        }
        if (isListRow(row)) {
            ++listRows;
        } else {
            ++continuousRows;
        }
    }
    REQUIRE(continuousRows == 84u + VoragoTest::kNumEcosystemRosterParams);  // 901, 902 continuous
    REQUIRE(listRows == 22u);

    std::size_t checked = 0;
    for (const auto& [index, id] : boundNodes(scan, tagMap)) {
        const auto& node = scan.nodes[static_cast<std::size_t>(index)];
        const std::string cls = node.get("class");
        CAPTURE(id, cls);
        const auto* row = findExpected(id);
        REQUIRE(row != nullptr);
        REQUIRE_FALSE(isHiddenRow(*row));

        std::string expected;
        if (id >= static_cast<int>(::Vorago::kMacroDarknessId) &&
            id <= static_cast<int>(::Vorago::kMacroMassId)) {
            expected = "ArcKnob";
        } else if (isListRow(*row)) {
            expected = (id == static_cast<int>(::Vorago::kSpaceFreezeId) ||
                        id == static_cast<int>(::Vorago::kGhostEventTriggersId))
                           ? "CCheckBox"
                           : "COptionMenu";
        } else {
            expected = (id == static_cast<int>(::Vorago::kMasterGainId) ||
                        id == static_cast<int>(::Vorago::kOutputSaturationId))
                           ? "CSlider"
                           : "ArcKnob";
        }
        REQUIRE(cls == expected);
        ++checked;
    }
    REQUIRE(checked == 106u + VoragoTest::kNumEcosystemRosterParams);
}

// ==============================================================================
// SC-005 - layout (C-1, FR-002 .. FR-006, FR-055)
// ==============================================================================
TEST_CASE("Vorago_UidescLayout", "[vorago][controller][ui]") {
    const std::string xml = readUidesc();
    const auto tagMap = Krate::Test::extractControlTagMap(xml);
    const auto scan = scanUidesc(xml);
    REQUIRE(scan.wellFormed);

    // --- template size == minSize == maxSize == 1100 x 760 -------------------
    const int root = findTemplate(scan, "editor");
    REQUIRE(root >= 0);
    {
        const auto& t = scan.nodes[static_cast<std::size_t>(root)];
        for (const char* key : {"size", "minSize", "maxSize"}) {
            CAPTURE(key);
            double w = 0.0;
            double h = 0.0;
            REQUIRE(parsePoint(t.get(key), w, h));
            REQUIRE(w == 1100.0);
            REQUIRE(h == 760.0);
        }
    }

    // --- the seven labelled C-1 regions, in window coordinates ---------------
    const std::array<std::pair<const char*, Rect>, 7> regions = {{
        {"header", Rect{.left = 0, .top = 0, .right = 1100, .bottom = 36}},
        {"concept-band", Rect{.left = 0, .top = 36, .right = 1100, .bottom = 436}},
        {"macros-left", Rect{.left = 0, .top = 36, .right = 350, .bottom = 436}},
        {"ecosystem", Rect{.left = 350, .top = 36, .right = 750, .bottom = 436}},
        {"macros-right", Rect{.left = 750, .top = 36, .right = 1100, .bottom = 436}},
        {"page-strip", Rect{.left = 0, .top = 436, .right = 1100, .bottom = 464}},
        {"page-area", Rect{.left = 0, .top = 464, .right = 1100, .bottom = 760}},
    }};
    std::map<std::string, int> regionNode;
    for (const auto& [label, rect] : regions) {
        CAPTURE(label);
        const auto found = findLabelled(scan, label);
        REQUIRE(found.size() == 1u);
        const auto& n = scan.nodes[static_cast<std::size_t>(found[0])];
        REQUIRE(isDescendantOf(scan, found[0], root));
        CAPTURE(n.window.left, n.window.top, n.window.right, n.window.bottom);
        REQUIRE(n.window == rect);
        regionNode[label] = found[0];
    }
    const int macrosLeft = regionNode["macros-left"];
    const int macrosRight = regionNode["macros-right"];
    const int pageArea = regionNode["page-area"];
    const Rect pageAreaRect = scan.nodes[static_cast<std::size_t>(pageArea)].window;

    const auto bound = boundNodes(scan, tagMap);

    // --- FR-003: macros >= 80 x 80, inside their block, in VoragoMacro order ---
    auto macroBlock = [&](int block, int firstId) {
        const Rect blockRect = scan.nodes[static_cast<std::size_t>(block)].window;
        std::vector<std::pair<int, int>> inBlock;  // (node, id)
        for (const auto& [index, id] : bound) {
            if (isDescendantOf(scan, index, block)) {
                inBlock.emplace_back(index, id);
            }
        }
        std::set<int> ids;
        for (const auto& e : inBlock) {
            ids.insert(e.second);
        }
        std::set<int> expected;
        for (int id = firstId; id < firstId + 6; ++id) {
            expected.insert(id);
        }
        REQUIRE(inBlock.size() == 6u);
        REQUIRE(ids == expected);
        for (const auto& [index, id] : inBlock) {
            const Rect r = scan.nodes[static_cast<std::size_t>(index)].window;
            CAPTURE(id, r.left, r.top, r.width(), r.height());
            REQUIRE(r.width() >= 80.0);
            REQUIRE(r.height() >= 80.0);
            REQUIRE(blockRect.contains(r));
        }
        // Row-major (top, left) order yields ascending IDs.
        std::sort(inBlock.begin(), inBlock.end(), [&](const auto& a, const auto& b) {
            const Rect ra = scan.nodes[static_cast<std::size_t>(a.first)].window;
            const Rect rb = scan.nodes[static_cast<std::size_t>(b.first)].window;
            if (ra.top != rb.top) {
                return ra.top < rb.top;
            }
            return ra.left < rb.left;
        });
        for (std::size_t i = 0; i < inBlock.size(); ++i) {
            CAPTURE(i);
            REQUIRE(inBlock[i].second == firstId + static_cast<int>(i));
        }
    };
    macroBlock(macrosLeft, static_cast<int>(::Vorago::kMacroDarknessId));   // 100-105
    macroBlock(macrosRight, static_cast<int>(::Vorago::kMacroPressureId));  // 106-111

    // --- FR-055: seven page containers, siblings under page-area, C-4 ID sets --
    const auto expectedPages = expectedPageIds();
    std::set<int> pageUnion;
    for (int k = 0; k < 7; ++k) {
        const std::string label = "page-" + std::to_string(k);
        CAPTURE(label);
        const auto found = findLabelled(scan, label);
        REQUIRE(found.size() == 1u);
        const auto& pn = scan.nodes[static_cast<std::size_t>(found[0])];
        REQUIRE(pn.get("class") == "CViewContainer");
        REQUIRE(pn.parent == pageArea);

        std::set<int> ids;
        for (const auto& [index, id] : bound) {
            if (isDescendantOf(scan, index, found[0])) {
                ids.insert(id);
            }
        }
        REQUIRE(ids == expectedPages[static_cast<std::size_t>(k)]);
        pageUnion.insert(ids.begin(), ids.end());
    }
    // 106 + roster bound - 4 header - 12 macros
    REQUIRE(pageUnion.size() == 90u + VoragoTest::kNumEcosystemRosterParams);

    // No page ID outside page-area; every page control inside the page-area rect;
    // every page knob <= 48 x 48.
    std::size_t pageKnobs = 0;
    for (const auto& [index, id] : bound) {
        if (!pageUnion.contains(id)) {
            continue;
        }
        const auto& n = scan.nodes[static_cast<std::size_t>(index)];
        CAPTURE(id, n.window.left, n.window.top, n.window.width(), n.window.height());
        REQUIRE(isDescendantOf(scan, index, pageArea));
        REQUIRE(pageAreaRect.contains(n.window));
        if (n.get("class") == "ArcKnob") {
            REQUIRE(n.window.width() <= 48.0);
            REQUIRE(n.window.height() <= 48.0);
            ++pageKnobs;
        }
    }
    REQUIRE(pageKnobs > 0u);

    // --- FR-006: every macro and page control has an untagged, mouse-disabled
    // CTextLabel sibling; a container holds at least as many such labels as it
    // holds macro/page controls (one label per control). ------------------------
    std::map<int, std::size_t> controlsPerParent;
    for (const auto& [index, id] : bound) {
        const bool isMacro = id >= static_cast<int>(::Vorago::kMacroDarknessId) &&
                             id <= static_cast<int>(::Vorago::kMacroMassId);
        if (!isMacro && !pageUnion.contains(id)) {
            continue;
        }
        const auto& n = scan.nodes[static_cast<std::size_t>(index)];
        REQUIRE(n.parent >= 0);
        ++controlsPerParent[n.parent];
    }
    std::size_t labelledControls = 0;
    for (const auto& [parent, controls] : controlsPerParent) {
        std::size_t labels = 0;
        for (const int child : scan.nodes[static_cast<std::size_t>(parent)].children) {
            const auto& c = scan.nodes[static_cast<std::size_t>(child)];
            if (c.get("class") == "CTextLabel" && !c.has("control-tag") &&
                c.get("mouse-enabled") == "false") {
                ++labels;
            }
        }
        CAPTURE(scan.nodes[static_cast<std::size_t>(parent)].get("uidesc-label"), controls, labels);
        REQUIRE(labels >= controls);
        labelledControls += controls;
    }
    REQUIRE(labelledControls == 12u + 90u + VoragoTest::kNumEcosystemRosterParams);

    // --- FR-005: class and custom-view-name whitelists (all views) -----------
    const std::set<std::string> allowedClasses = {"CViewContainer", "CTextLabel", "CSlider",
                                                  "COptionMenu",    "CCheckBox",  "CSegmentButton",
                                                  "CView",          "ArcKnob"};
    const std::set<std::string> allowedCustom = {"EcosystemView", "PresetBrowserButton"};
    std::map<std::string, int> customCounts;
    for (const auto& n : scan.nodes) {
        const std::string cls = n.get("class");
        CAPTURE(n.element, cls, n.get("uidesc-label"));
        REQUIRE(allowedClasses.count(cls) == 1u);
        if (n.has("custom-view-name")) {
            const std::string custom = n.get("custom-view-name");
            CAPTURE(custom);
            REQUIRE(allowedCustom.count(custom) == 1u);
            ++customCounts[custom];
        }
    }
    // C-1: one ecosystem view, one header preset button (FR-070).
    REQUIRE(customCounts["EcosystemView"] == 1);
    REQUIRE(customCounts["PresetBrowserButton"] == 1);
    for (const int ecosystem : findLabelled(scan, "ecosystem")) {
        REQUIRE(scan.nodes[static_cast<std::size_t>(ecosystem)].get("custom-view-name") ==
                "EcosystemView");
    }
}

// ==============================================================================
// SC-028 - the Phase 14 ecosystem roster knobs on page 6 (FR-073)
// ==============================================================================
TEST_CASE("Vorago_Ecosystem_PageBindsRosterIds", "[vorago][ui]") {
    const std::string xml = readUidesc();
    const auto tagMap = Krate::Test::extractControlTagMap(xml);
    const auto scan = scanUidesc(xml);
    REQUIRE(scan.wellFormed);

    const auto page6 = findLabelled(scan, "page-6");
    REQUIRE(page6.size() == 1u);
    const int page = page6[0];
    const Rect pageRect = scan.nodes[static_cast<std::size_t>(page)].window;
    REQUIRE(pageRect.width() == 1100.0);
    REQUIRE(pageRect.height() == 296.0);

    // Every registered ID bound somewhere; the unbound set is exactly {4, 5}.
    const auto bound = boundNodes(scan, tagMap);
    std::set<int> boundIds;
    for (const auto& e : bound) {
        boundIds.insert(e.second);
    }
    std::set<int> unbound;
    for (const auto& row : VoragoTest::kExpectedParams) {
        if (!boundIds.contains(static_cast<int>(row.id))) {
            unbound.insert(static_cast<int>(row.id));
        }
    }
    const std::set<int> allowlist = {static_cast<int>(::Vorago::kSustainPedalId),
                                     static_cast<int>(::Vorago::kChannelPressureId)};
    CAPTURE(unbound);
    REQUIRE(unbound == allowlist);

    // 901 and 902: each bound exactly once, an ArcKnob under page-6, inside the
    // 1100 x 296 page, overlapping no other view on the page.
    for (const int rosterId : {static_cast<int>(::Vorago::kEcosystemSyncRateId),
                               static_cast<int>(::Vorago::kEcosystemSelfAffinityId)}) {
        CAPTURE(rosterId);
        std::vector<int> nodes;
        for (const auto& [index, id] : bound) {
            if (id == rosterId) {
                nodes.push_back(index);
            }
        }
        REQUIRE(nodes.size() == 1u);
        const int knob = nodes[0];
        const auto& kn = scan.nodes[static_cast<std::size_t>(knob)];
        REQUIRE(kn.get("class") == "ArcKnob");
        REQUIRE(isDescendantOf(scan, knob, page));
        const Rect r = kn.window;
        CAPTURE(r.left, r.top, r.right, r.bottom);
        REQUIRE(r.width() > 0.0);
        REQUIRE(r.height() > 0.0);
        REQUIRE(pageRect.contains(r));

        for (std::size_t i = 0; i < scan.nodes.size(); ++i) {
            const int other = static_cast<int>(i);
            if (other == knob || !isDescendantOf(scan, other, page)) {
                continue;
            }
            const Rect o = scan.nodes[i].window;
            const bool overlaps =
                r.left < o.right && o.left < r.right && r.top < o.bottom && o.top < r.bottom;
            CAPTURE(scan.nodes[i].get("class"), scan.nodes[i].get("control-tag"),
                    scan.nodes[i].get("title"), o.left, o.top, o.right, o.bottom);
            REQUIRE_FALSE(overlaps);
        }
    }
}

// ==============================================================================
// SC-017 - page switch (C-5, FR-055, FR-056)
// ==============================================================================
TEST_CASE("Vorago_Editor_PageSwitch", "[vorago][controller][ui]") {
    auto controller = Steinberg::owned(new ::Vorago::Controller());
    REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);

    {
        EditorSession session(*controller);
        REQUIRE(session.attached());
        Surface s = collectSurface(session.editor());

        for (int k = 0; k < 7; ++k) {
            CAPTURE(k);
            REQUIRE(s.pages[static_cast<std::size_t>(k)] != nullptr);
        }
        REQUIRE(s.segmentCount == 1);
        REQUIRE(s.segment != nullptr);
        REQUIRE(s.segment->getSegments().size() == 7u);
        REQUIRE(s.segment->getTag() >= ::Vorago::UI::kSessionTagBase);
        REQUIRE(s.ecosystemCount == 1);
        REQUIRE(s.ecosystem.view == controller->ecosystemViewForTest());
        REQUIRE(s.macros.size() == 12u);

        // FR-055 default: first open of a fresh controller shows page 0.
        REQUIRE(onlyPageVisible(s, 0));
        REQUIRE(s.segment->getSelectedSegment() == 0u);
        REQUIRE(controller->activePage() == 0);

        const VSTGUI::CRect ecoRect = s.ecosystem.window;
        REQUIRE(ecoRect == VSTGUI::CRect(350, 36, 750, 436));
        std::map<int, VSTGUI::CRect> macroRects;
        for (const auto& [tag, rec] : s.macros) {
            macroRects[tag] = rec.window;
        }

        for (int k = 0; k < 7; ++k) {
            CAPTURE(k);
            s.segment->setSelectedSegment(static_cast<std::uint32_t>(k));
            s.segment->valueChanged();

            REQUIRE(onlyPageVisible(s, k));
            REQUIRE(controller->activePage() == k);

            // FR-056: concept band untouched - re-walk the built tree.
            const Surface now = collectSurface(session.editor());
            REQUIRE(now.ecosystemCount == 1);
            REQUIRE(now.ecosystem.view == s.ecosystem.view);
            REQUIRE(now.ecosystem.effectiveVisible);
            REQUIRE(now.ecosystem.window == ecoRect);
            REQUIRE(now.macros.size() == 12u);
            for (const auto& [tag, rec] : now.macros) {
                CAPTURE(tag);
                REQUIRE(rec.effectiveVisible);
                REQUIRE(rec.window == macroRects[tag]);
            }
            // The page containers stay mounted at a constant rect (no remove/resize).
            for (int i = 0; i < 7; ++i) {
                CAPTURE(i);
                REQUIRE(now.pages[static_cast<std::size_t>(i)] == s.pages[static_cast<std::size_t>(i)]);
                REQUIRE(now.pageRecs[static_cast<std::size_t>(i)].window ==
                        s.pageRecs[static_cast<std::size_t>(i)].window);
            }
        }
    }

    // Close, reopen -> still page 6 (the last one selected).
    {
        EditorSession session(*controller);
        REQUIRE(session.attached());
        Surface s = collectSurface(session.editor());
        REQUIRE(s.segment != nullptr);
        REQUIRE(onlyPageVisible(s, 6));
        REQUIRE(s.segment->getSelectedSegment() == 6u);

        // Select a middle page, then reopen once more.
        s.segment->setSelectedSegment(2u);
        s.segment->valueChanged();
        REQUIRE(onlyPageVisible(s, 2));
    }
    {
        EditorSession session(*controller);
        REQUIRE(session.attached());
        const Surface s = collectSurface(session.editor());
        REQUIRE(s.segment != nullptr);
        REQUIRE(onlyPageVisible(s, 2));
        REQUIRE(s.segment->getSelectedSegment() == 2u);
    }

    REQUIRE(controller->terminate() == Steinberg::kResultOk);
}

// ==============================================================================
// SC-018 (session arm) - page switches and the preset button edit no parameter
// ==============================================================================
TEST_CASE("Vorago_Editor_PageSwitchIsSessionOnly", "[vorago][controller][ui]") {
    RecordingHandler handler;  // declared first: outlives the controller
    auto controller = Steinberg::owned(new ::Vorago::Controller());
    REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);
    REQUIRE(controller->setComponentHandler(&handler) == Steinberg::kResultOk);

    constexpr auto kRegistered =
        static_cast<Steinberg::int32>(108 + VoragoTest::kNumEcosystemRosterParams);
    REQUIRE(controller->getParameterCount() == kRegistered);
    const auto before = snapshotParams(*controller);
    REQUIRE(before.size() == 108u + VoragoTest::kNumEcosystemRosterParams);

    {
        EditorSession session(*controller);
        REQUIRE(session.attached());
        Surface s = collectSurface(session.editor());
        REQUIRE(s.segment != nullptr);
        REQUIRE(s.segment->getTag() >= ::Vorago::UI::kSessionTagBase);

        // All seven pages, with repeats, back to 0. Driven as a user drag would
        // be: the listener sees begin/value/end with the session tag.
        const std::array<int, 12> script = {1, 2, 2, 3, 4, 5, 6, 6, 3, 0, 5, 0};
        for (const int k : script) {
            CAPTURE(k);
            s.segment->beginEdit();
            s.segment->setSelectedSegment(static_cast<std::uint32_t>(k));
            s.segment->valueChanged();
            s.segment->endEdit();
            REQUIRE(onlyPageVisible(s, k));  // non-vacuity: the switch was routed
            REQUIRE(handler.total() == 0);
        }

        // The header preset button: the full OutlineBrowserButton::onMouseDown
        // triple (outline_button.h:79-131).
        REQUIRE(s.presetButtonCount == 1);
        REQUIRE(s.presetButton != nullptr);
        REQUIRE(s.presetButton->getTag() >= ::Vorago::UI::kSessionTagBase);
        VSTGUI::CPoint where(1, 1);
        const VSTGUI::CButtonState buttons(VSTGUI::kLButton);
        (void)s.presetButton->onMouseDown(where, buttons);

        // Non-vacuity: the click was routed to openPresetBrowser().
        const auto* browser = controller->presetBrowserViewForTest();
        REQUIRE(browser != nullptr);
        REQUIRE(browser->isOpen());
        REQUIRE(handler.total() == 0);
    }

    REQUIRE(handler.begins == 0);
    REQUIRE(handler.performs == 0);
    REQUIRE(handler.ends == 0);
    REQUIRE(controller->getParameterCount() == kRegistered);
    const auto after = snapshotParams(*controller);
    REQUIRE(after == before);

    REQUIRE(controller->terminate() == Steinberg::kResultOk);
}

// ==============================================================================
// SC-022 - Gravity anchor-mode display (FR-080, FR-081)
// ==============================================================================
TEST_CASE("Vorago_GravityMacro_AnchorModeDisplay", "[vorago][controller][ui]") {
    const std::string normalTip = uidescGravityTooltip();
    const double kFree = ::Vorago::indexToNormalized(0, ::Vorago::kNumResonanceAnchorModes);
    const double kKeyed = ::Vorago::indexToNormalized(1, ::Vorago::kNumResonanceAnchorModes);
    const double kHybrid = ::Vorago::indexToNormalized(2, ::Vorago::kNumResonanceAnchorModes);
    REQUIRE(::Vorago::indexFromNormalized(kHybrid, ::Vorago::kNumResonanceAnchorModes) ==
            ::Vorago::kResonanceAnchorModeDefault);

    // The knob's interactivity is never touched by the display update.
    struct KnobState {
        float alpha = 0.0f;
        bool mouseEnabled = false;
        float value = 0.0f;
    };
    auto knobState = [](VSTGUI::CControl* knob) {
        return KnobState{.alpha = knob->getAlphaValue(),
                         .mouseEnabled = knob->getMouseEnabled(),
                         .value = knob->getValueNormalized()};
    };
    auto sameKnob = [](const KnobState& a, const KnobState& b) {
        return a.alpha == b.alpha && a.mouseEnabled == b.mouseEnabled && a.value == b.value;
    };

    auto expectInert = [&](const Surface& s, ::Vorago::Controller& c) {
        REQUIRE(s.gravityKnob != nullptr);
        REQUIRE(s.gravityLabel != nullptr);
        const std::string tip = tooltipOf(s.gravityKnob);
        CAPTURE(tip);
        REQUIRE_FALSE(tip.empty());
        REQUIRE(tip != normalTip);
        REQUIRE(toLower(tip).find("inert") != std::string::npos);
        REQUIRE(s.gravityLabel->getText().getString() == "Gravity (inert)");
        REQUIRE(c.gravityDisplayInertForTest());
    };
    auto expectNormal = [&](const Surface& s, ::Vorago::Controller& c) {
        REQUIRE(s.gravityKnob != nullptr);
        REQUIRE(s.gravityLabel != nullptr);
        REQUIRE(tooltipOf(s.gravityKnob) == normalTip);
        REQUIRE(s.gravityLabel->getText().getString() == "Gravity");
        REQUIRE_FALSE(c.gravityDisplayInertForTest());
    };
    auto checkSurface = [](const Surface& s) {
        REQUIRE(s.gravityKnob != nullptr);
        REQUIRE(dynamic_cast<Krate::Plugins::ArcKnob*>(s.gravityKnob) != nullptr);
        REQUIRE(s.gravityLabelCount == 1);
    };

    SECTION("ArmA_FreeCurrentAtOpen") {
        RecordingHandler handler;
        auto controller = Steinberg::owned(new ::Vorago::Controller());
        REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);
        REQUIRE(controller->setComponentHandler(&handler) == Steinberg::kResultOk);
        REQUIRE(controller->setParamNormalized(::Vorago::kResonanceAnchorModeId, kFree) ==
                Steinberg::kResultOk);
        {
            EditorSession session(*controller);
            REQUIRE(session.attached());
            const Surface s = collectSurface(session.editor());
            checkSurface(s);
            const KnobState k0 = knobState(s.gravityKnob);
            expectInert(s, *controller);
            REQUIRE(sameKnob(knobState(s.gravityKnob), k0));
            REQUIRE(k0.mouseEnabled);
            REQUIRE(k0.alpha == 1.0f);
        }
        REQUIRE(handler.total() == 0);
        REQUIRE(controller->terminate() == Steinberg::kResultOk);
    }

    SECTION("ArmAPrime_KeyedCurrentAtOpen") {
        RecordingHandler handler;
        auto controller = Steinberg::owned(new ::Vorago::Controller());
        REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);
        REQUIRE(controller->setComponentHandler(&handler) == Steinberg::kResultOk);
        REQUIRE(controller->setParamNormalized(::Vorago::kResonanceAnchorModeId, kKeyed) ==
                Steinberg::kResultOk);
        {
            EditorSession session(*controller);
            REQUIRE(session.attached());
            const Surface s = collectSurface(session.editor());
            checkSurface(s);
            const KnobState k0 = knobState(s.gravityKnob);
            expectInert(s, *controller);
            REQUIRE(sameKnob(knobState(s.gravityKnob), k0));
            REQUIRE(k0.mouseEnabled);
            REQUIRE(k0.alpha == 1.0f);
        }
        REQUIRE(handler.total() == 0);
        REQUIRE(controller->terminate() == Steinberg::kResultOk);
    }

    SECTION("ArmB_ChangeWhileClosed") {
        RecordingHandler handler;
        auto controller = Steinberg::owned(new ::Vorago::Controller());
        REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);
        REQUIRE(controller->setComponentHandler(&handler) == Steinberg::kResultOk);
        REQUIRE(controller->setParamNormalized(::Vorago::kResonanceAnchorModeId, kHybrid) ==
                Steinberg::kResultOk);
        {
            EditorSession session(*controller);
            REQUIRE(session.attached());
            const Surface s = collectSurface(session.editor());
            checkSurface(s);
            expectNormal(s, *controller);
        }
        REQUIRE(controller->setParamNormalized(::Vorago::kResonanceAnchorModeId, kFree) ==
                Steinberg::kResultOk);
        {
            EditorSession session(*controller);
            REQUIRE(session.attached());
            const Surface s = collectSurface(session.editor());
            checkSurface(s);
            const KnobState k0 = knobState(s.gravityKnob);
            expectInert(s, *controller);
            REQUIRE(sameKnob(knobState(s.gravityKnob), k0));
        }
        REQUIRE(controller->setParamNormalized(::Vorago::kResonanceAnchorModeId, kHybrid) ==
                Steinberg::kResultOk);
        {
            EditorSession session(*controller);
            REQUIRE(session.attached());
            const Surface s = collectSurface(session.editor());
            checkSurface(s);
            const KnobState k0 = knobState(s.gravityKnob);
            expectNormal(s, *controller);  // catches a stale inert flag
            REQUIRE(sameKnob(knobState(s.gravityKnob), k0));
        }
        REQUIRE(handler.total() == 0);
        REQUIRE(controller->terminate() == Steinberg::kResultOk);
    }

    SECTION("ArmC_LiveWhileOpen") {
        RecordingHandler handler;
        auto controller = Steinberg::owned(new ::Vorago::Controller());
        REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);
        REQUIRE(controller->setComponentHandler(&handler) == Steinberg::kResultOk);
        REQUIRE(controller->setParamNormalized(::Vorago::kResonanceAnchorModeId, kHybrid) ==
                Steinberg::kResultOk);
        const double gravityBefore = controller->getParamNormalized(::Vorago::kMacroGravityId);
        {
            EditorSession session(*controller);
            REQUIRE(session.attached());
            const Surface s = collectSurface(session.editor());
            checkSurface(s);
            const KnobState k0 = knobState(s.gravityKnob);
            expectNormal(s, *controller);

            // No re-open between these: the observer updates synchronously.
            REQUIRE(controller->setParamNormalized(::Vorago::kResonanceAnchorModeId, kFree) ==
                    Steinberg::kResultOk);
            expectInert(s, *controller);
            REQUIRE(sameKnob(knobState(s.gravityKnob), k0));

            REQUIRE(controller->setParamNormalized(::Vorago::kResonanceAnchorModeId, kKeyed) ==
                    Steinberg::kResultOk);
            expectInert(s, *controller);
            REQUIRE(sameKnob(knobState(s.gravityKnob), k0));

            REQUIRE(controller->setParamNormalized(::Vorago::kResonanceAnchorModeId, kHybrid) ==
                    Steinberg::kResultOk);
            expectNormal(s, *controller);
            REQUIRE(sameKnob(knobState(s.gravityKnob), k0));

            // The display wrote no parameter: 104 and 403 are where the test put them.
            REQUIRE(controller->getParamNormalized(::Vorago::kResonanceAnchorModeId) == kHybrid);
            REQUIRE(controller->getParamNormalized(::Vorago::kMacroGravityId) == gravityBefore);
        }
        REQUIRE(handler.total() == 0);
        REQUIRE(controller->terminate() == Steinberg::kResultOk);
    }
}
