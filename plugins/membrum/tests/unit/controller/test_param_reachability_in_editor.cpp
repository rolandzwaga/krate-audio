// ==============================================================================
// T034 / T035: editor.uidesc parameter reachability + mode-toggle idempotence
// Spec: specs/141-membrum-phase6-ui/spec.md (FR-026, SC-002, SC-003)
// ==============================================================================
//
// T034 -- Every parameter registered by Controller::initialize() must be
//         reachable from the editor XML:
//           * directly as a <control-tag> value, OR
//           * via the Phase 4 selected-pad proxy pattern (control-tag equals
//             the global proxy ID; selectedPadIndex routes to the current pad),
//             OR
//           * via a pad-0-anchored per-pad control-tag for any pad (the editor
//             binds pad 0's per-pad parameter IDs directly for selected-pad
//             macros), OR
//           * session-scoped (kUiModeId).
//
//         In addition, the Extended-mode template must contain control-tags
//         for every Unnatural Zone, raw physics, full Tone Shaper, full
//         Exciter, Material Morph, and per-pad Coupling parameter exposed in
//         Phase 1-5. (Macros live on the Acoustic template; they are not
//         required to appear in Extended.)
//
// T035 -- Toggling kUiModeId 10 times does NOT modify any other registered
//         parameter. Hiding is purely visual.
// ==============================================================================

#include "controller/controller.h"
#include "plugin_ids.h"
#include "dsp/pad_config.h"

#include "pluginterfaces/vst/vsttypes.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace Membrum;

namespace {

// ------------------------------------------------------------------------------
// Locate editor.uidesc by walking up from the test executable's CWD.
// In CTest runs the cwd is usually the build-output directory; the source tree
// is stable relative to the repository root.
// ------------------------------------------------------------------------------
std::filesystem::path findEditorUidesc()
{
    namespace fs = std::filesystem;
    const fs::path relative = "plugins/membrum/resources/editor.uidesc";

    // Walk up from cwd.
    fs::path cur = fs::current_path();
    for (int i = 0; i < 10; ++i) {
        const auto candidate = cur / relative;
        if (fs::exists(candidate))
            return candidate;
        if (!cur.has_parent_path() || cur.parent_path() == cur)
            break;
        cur = cur.parent_path();
    }

    // Fallback: some test runners use the source dir directly.
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

// Extract every integer value from tag="NNN" attributes inside <control-tag>
// elements. A lightweight regex-free scanner is sufficient: the XML is
// hand-authored and all control-tag entries use the pattern
// `<control-tag name="..." tag="NNN"/>`.
std::set<int> extractControlTagIds(const std::string& xml)
{
    std::set<int> ids;
    const std::string marker = "<control-tag";
    std::size_t pos = 0;
    while ((pos = xml.find(marker, pos)) != std::string::npos) {
        const std::size_t tagEnd = xml.find('>', pos);
        if (tagEnd == std::string::npos)
            break;
        const auto line = xml.substr(pos, tagEnd - pos);
        const auto tagPos = line.find("tag=\"");
        if (tagPos != std::string::npos) {
            const auto numStart = tagPos + 5;
            const auto numEnd = line.find('"', numStart);
            if (numEnd != std::string::npos) {
                const auto numStr = line.substr(numStart, numEnd - numStart);
                try {
                    ids.insert(std::stoi(numStr));
                } catch (...) {
                    // ignore non-numeric
                }
            }
        }
        pos = tagEnd + 1;
    }
    return ids;
}

// Extract the content of a named <template ...>...</template> block.
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

// Extract every `control-tag="NAME"` value referenced inside a template block.
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

// Map of global proxy parameter ID -> pad offset (mirrors controller.cpp's
// kProxyMappings). A global is reachable if the editor binds either the
// global ID directly, the per-pad pad-0 anchor of its offset, or any other
// anchor of the same offset.
struct GlobalProxyPair { int globalId; int padOffset; };
std::vector<GlobalProxyPair> globalProxyMap()
{
    return {
        { kMaterialId, kPadMaterial },
        { kSizeId, kPadSize },
        { kDecayId, kPadDecay },
        { kStrikePositionId, kPadStrikePosition },
        { kLevelId, kPadLevel },
        { kExciterTypeId, kPadExciterType },
        { kBodyModelId, kPadBodyModel },
        { kExciterFMRatioId, kPadFMRatio },
        { kExciterFeedbackAmountId, kPadFeedbackAmount },
        { kExciterNoiseBurstDurationId, kPadNoiseBurstDuration },
        { kExciterFrictionPressureId, kPadFrictionPressure },
        { kToneShaperFilterTypeId, kPadTSFilterType },
        { kToneShaperFilterCutoffId, kPadTSFilterCutoff },
        { kToneShaperFilterResonanceId, kPadTSFilterResonance },
        { kToneShaperFilterEnvAmountId, kPadTSFilterEnvAmount },
        { kToneShaperDriveAmountId, kPadTSDriveAmount },
        { kToneShaperFoldAmountId, kPadTSFoldAmount },
        { kToneShaperPitchEnvStartId, kPadTSPitchEnvStart },
        { kToneShaperPitchEnvEndId, kPadTSPitchEnvEnd },
        { kToneShaperPitchEnvTimeId, kPadTSPitchEnvTime },
        { kToneShaperPitchEnvCurveId, kPadTSPitchEnvCurve },
        { kToneShaperFilterEnvAttackId, kPadTSFilterEnvAttack },
        { kToneShaperFilterEnvDecayId, kPadTSFilterEnvDecay },
        { kToneShaperFilterEnvSustainId, kPadTSFilterEnvSustain },
        { kToneShaperFilterEnvReleaseId, kPadTSFilterEnvRelease },
        { kUnnaturalModeStretchId, kPadModeStretch },
        { kUnnaturalDecaySkewId, kPadDecaySkew },
        { kUnnaturalModeInjectAmountId, kPadModeInjectAmount },
        { kUnnaturalNonlinearCouplingId, kPadNonlinearCoupling },
        { kMorphEnabledId, kPadMorphEnabled },
        { kMorphStartId, kPadMorphStart },
        { kMorphEndId, kPadMorphEnd },
        { kMorphDurationMsId, kPadMorphDuration },
        { kMorphCurveId, kPadMorphCurve },
        { kChokeGroupId, kPadChokeGroup },
        // Phase 8 (T074 / US7 / FR-065): Output Bus selector global proxy.
        { kOutputBusId, kPadOutputBus },
        // Phase 7: parallel noise layer + always-on click transient proxies.
        { kNoiseLayerMixId, kPadNoiseLayerMix },
        { kNoiseLayerCutoffId, kPadNoiseLayerCutoff },
        { kNoiseLayerResonanceId, kPadNoiseLayerResonance },
        { kNoiseLayerDecayId, kPadNoiseLayerDecay },
        { kNoiseLayerColorId, kPadNoiseLayerColor },
        { kClickLayerMixId, kPadClickLayerMix },
        { kClickLayerContactMsId, kPadClickLayerContactMs },
        { kClickLayerBrightnessId, kPadClickLayerBrightness },
        // Phase 8A: per-mode damping law global proxies.
        { kBodyDampingB1Id, kPadBodyDampingB1 },
        { kBodyDampingB3Id, kPadBodyDampingB3 },
        // Phase 8C: air-loading + per-mode scatter global proxies.
        { kAirLoadingId,    kPadAirLoading },
        { kModeScatterId,   kPadModeScatter },
        // Phase 8D: head <-> shell coupling global proxies.
        { kCouplingStrengthId,   kPadCouplingStrength },
        { kSecondaryEnabledId,   kPadSecondaryEnabled },
        { kSecondarySizeId,      kPadSecondarySize },
        { kSecondaryMaterialId,  kPadSecondaryMaterial },
    };
}

// Build the offset -> global proxy ID map (mirrors controller.cpp's
// kProxyMappings table). Any per-pad parameter at an offset present in this
// map is reachable via the selected-pad proxy when the global ID is wired
// into the editor.
std::set<int> proxiedOffsets()
{
    return {
        kPadMaterial, kPadSize, kPadDecay, kPadStrikePosition, kPadLevel,
        kPadExciterType, kPadBodyModel,
        kPadFMRatio, kPadFeedbackAmount, kPadNoiseBurstDuration, kPadFrictionPressure,
        kPadTSFilterType, kPadTSFilterCutoff, kPadTSFilterResonance, kPadTSFilterEnvAmount,
        kPadTSDriveAmount, kPadTSFoldAmount,
        kPadTSPitchEnvStart, kPadTSPitchEnvEnd, kPadTSPitchEnvTime, kPadTSPitchEnvCurve,
        kPadTSFilterEnvAttack, kPadTSFilterEnvDecay, kPadTSFilterEnvSustain, kPadTSFilterEnvRelease,
        kPadModeStretch, kPadDecaySkew, kPadModeInjectAmount, kPadNonlinearCoupling,
        kPadMorphEnabled, kPadMorphStart, kPadMorphEnd, kPadMorphDuration, kPadMorphCurve,
        kPadChokeGroup,
        // Phase 8 (T074 / US7 / FR-065): Output Bus offset is now proxied.
        kPadOutputBus,
        // Phase 7: parallel noise layer + always-on click transient offsets.
        kPadNoiseLayerMix, kPadNoiseLayerCutoff, kPadNoiseLayerResonance,
        kPadNoiseLayerDecay, kPadNoiseLayerColor,
        kPadClickLayerMix, kPadClickLayerContactMs, kPadClickLayerBrightness,
        // Phase 8A: per-mode damping law offsets.
        kPadBodyDampingB1, kPadBodyDampingB3,
        // Phase 8C: air-loading + per-mode scatter offsets.
        kPadAirLoading, kPadModeScatter,
        // Phase 8D: head <-> shell coupling offsets.
        kPadCouplingStrength, kPadSecondaryEnabled,
        kPadSecondarySize, kPadSecondaryMaterial,
    };
}

int offsetOfPadParam(int paramId)
{
    return Membrum::padOffsetFromParamId(paramId);
}

int padIndexOfPadParam(int paramId)
{
    return Membrum::padIndexFromParamId(paramId);
}

} // namespace

// ==============================================================================
// T034: reachability
// ==============================================================================
TEST_CASE("editor.uidesc reaches every registered parameter (SC-002)",
          "[editor_reachability]")
{
    Controller ctl;
    REQUIRE(ctl.initialize(nullptr) == Steinberg::kResultOk);

    const auto uidescPath = findEditorUidesc();
    REQUIRE_FALSE(uidescPath.empty());
    const auto xml = slurp(uidescPath);
    REQUIRE_FALSE(xml.empty());

    const auto controlTagIds = extractControlTagIds(xml);

    // Collect the set of offsets that are reachable as pad-0-anchored
    // per-pad control-tags in the editor (e.g. tag=1037 -> pad 0 offset 37).
    std::set<int> reachableOffsetsViaPerPadTag;
    for (int tag : controlTagIds) {
        const int off = offsetOfPadParam(tag);
        if (off >= 0)
            reachableOffsetsViaPerPadTag.insert(off);
    }

    const auto proxied = proxiedOffsets();

    // Global proxy IDs: a global is reachable if its corresponding pad offset
    // is reachable through any pad-anchored control-tag in the editor.
    const auto proxyMap = globalProxyMap();
    std::set<int> reachableGlobalProxyIds;
    for (const auto& pair : proxyMap) {
        if (reachableOffsetsViaPerPadTag.count(pair.padOffset) != 0 ||
            controlTagIds.count(pair.globalId) != 0)
            reachableGlobalProxyIds.insert(pair.globalId);
    }

    const int paramCount = ctl.getParameterCount();
    REQUIRE(paramCount > 0);

    std::vector<int> unreachable;

    for (int i = 0; i < paramCount; ++i) {
        Steinberg::Vst::ParameterInfo info{};
        REQUIRE(ctl.getParameterInfo(i, info) == Steinberg::kResultOk);
        const int id = static_cast<int>(info.id);

        // Session-scoped (FR-026 exemption): ui mode
        if (id == kUiModeId)
            continue;

        // Directly control-tagged
        if (controlTagIds.count(id) != 0)
            continue;

        // Global proxy parameter: reachable if its pad offset is exposed
        // anywhere in the editor (sub-controller routes the proxy through
        // kSelectedPadId).
        if (reachableGlobalProxyIds.count(id) != 0)
            continue;

        // Per-pad parameter: reachable via (a) any pad-anchored per-pad tag
        // for the same offset, or (b) a global proxy control-tag whose
        // offset matches, or (c) kSelectedPadId control-tag plus the
        // offset being in the proxied set.
        const int offset = offsetOfPadParam(id);
        if (offset >= 0) {
            if (reachableOffsetsViaPerPadTag.count(offset) != 0)
                continue;
            if (proxied.count(offset) != 0 &&
                controlTagIds.count(kSelectedPadId) != 0)
                continue;
        }

        unreachable.push_back(id);
    }

    if (!unreachable.empty()) {
        std::ostringstream msg;
        msg << "Unreachable parameter IDs (" << unreachable.size() << "): ";
        for (std::size_t i = 0; i < std::min<std::size_t>(unreachable.size(), 32); ++i) {
            if (i) msg << ", ";
            const int id = unreachable[i];
            const int pad = padIndexOfPadParam(id);
            const int off = offsetOfPadParam(id);
            msg << id;
            if (pad >= 0)
                msg << "(pad" << pad << "off" << off << ")";
        }
        if (unreachable.size() > 32)
            msg << ", ...";
        FAIL(msg.str());
    }

    ctl.terminate();
}

// ------------------------------------------------------------------------------
// T034: Extended template must expose Unnatural Zone, raw physics, full Tone
// Shaper, full Exciter, Material Morph, and per-pad Coupling controls.
// ------------------------------------------------------------------------------
TEST_CASE("editor.uidesc Extended template contains required control-tags",
          "[editor_reachability]")
{
    const auto uidescPath = findEditorUidesc();
    REQUIRE_FALSE(uidescPath.empty());
    const auto xml = slurp(uidescPath);
    REQUIRE_FALSE(xml.empty());

    const auto extended = extractTemplate(xml, "SelectedPadExtended");
    REQUIRE_FALSE(extended.empty());

    const auto refs = extractTagRefs(extended);

    // Required control-tag names that MUST appear in the Extended template.
    const std::vector<std::string> required = {
        // Unnatural Zone
        "ModeStretch", "DecaySkew", "ModeInject", "NonlinearCoupling",
        "MaterialMorph",
        // Raw physics (Phase 1 core)
        "Material", "Size", "Decay", "StrikePosition", "Level",
        // Full Tone Shaper
        "FilterType", "FilterCutoff", "FilterResonance", "FilterEnvAmount",
        "DriveAmount", "FoldAmount",
        "FilterEnvAttack", "FilterEnvDecay", "FilterEnvSustain", "FilterEnvRelease",
        // Full Exciter
        "ExciterFMRatio", "ExciterFeedback", "ExciterNoiseBurstDuration",
        "ExciterFrictionPressure",
        // Per-pad Coupling Amount
        "CouplingAmount",
    };

    std::vector<std::string> missing;
    for (const auto& r : required) {
        if (refs.count(r) == 0)
            missing.push_back(r);
    }
    if (!missing.empty()) {
        std::ostringstream msg;
        msg << "Extended template missing control-tags: ";
        for (std::size_t i = 0; i < missing.size(); ++i) {
            if (i) msg << ", ";
            msg << missing[i];
        }
        FAIL(msg.str());
    }
}

// ==============================================================================
// T035: Toggling kUiModeId 10x does not mutate any other parameter (SC-003).
// ==============================================================================
TEST_CASE("kUiModeId toggling does not change other registered parameters (SC-003)",
          "[editor_reachability]")
{
    Controller ctl;
    REQUIRE(ctl.initialize(nullptr) == Steinberg::kResultOk);

    const int paramCount = ctl.getParameterCount();
    REQUIRE(paramCount > 0);

    struct Snap { Steinberg::Vst::ParamID id; Steinberg::Vst::ParamValue value; };
    std::vector<Snap> before;
    before.reserve(static_cast<std::size_t>(paramCount));
    for (int i = 0; i < paramCount; ++i) {
        Steinberg::Vst::ParameterInfo info{};
        REQUIRE(ctl.getParameterInfo(i, info) == Steinberg::kResultOk);
        if (info.id == kUiModeId)
            continue;
        before.push_back({ info.id, ctl.getParamNormalized(info.id) });
    }

    for (int toggle = 0; toggle < 10; ++toggle) {
        const Steinberg::Vst::ParamValue v = (toggle % 2 == 0) ? 1.0 : 0.0;
        REQUIRE(ctl.setParamNormalized(kUiModeId, v) == Steinberg::kResultOk);

        for (const auto& s : before) {
            const auto now = ctl.getParamNormalized(s.id);
            if (now != s.value) {
                std::ostringstream msg;
                msg << "kUiModeId toggle " << toggle
                    << " mutated param " << s.id
                    << " (" << s.value << " -> " << now << ")";
                FAIL(msg.str());
            }
        }
    }

    ctl.terminate();
}
