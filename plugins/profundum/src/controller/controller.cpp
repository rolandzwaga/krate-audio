// ==============================================================================
// Profundum - Edit Controller implementation   Plan section 2.6
// ==============================================================================
// T010: initialize registers the eight Phase 10 parameters (global pack, then
// macro pack) and getParamStringByValue formats the continuous ones (FR-047).
// T012: setComponentState mirrors the processor stream (FR-045, FR-046).
// T019: the PresetManager (FR-050) and createView on the stock-view placeholder
// editor.uidesc (FR-054, FR-055). No UpdateChecker (FR-052).
// Phase 11 T014: initialize registers all 84 parameters (global, macro,
// performance, sound, note, morph control) and getParamStringByValue formats
// every continuous one (FR-001 - FR-009).
// Phase 11 T016: the endpoint mirror (FR-021), the Edit Target re-display and
// restart (FR-022, FR-024) and the endpoint / compare-mode notify (FR-025).
// Phase 11 T019: setComponentState parses state v1 and v2 (FR-044, FR-045).
// Phase 11 T021: IMidiMapping::getMidiControllerAssignment (FR-050).
// ==============================================================================

#include "controller/controller.h"

#include "parameters/global_params.h"
#include "parameters/macro_params.h"
#include "parameters/morph_params.h"
#include "parameters/note_params.h"
#include "parameters/performance_params.h"
#include "parameters/sound_param_row.h"
#include "parameters/sound_param_table.h"
#include "plugin_ids.h"
#include "preset/profundum_preset_config.h"

#include "pluginterfaces/vst/ivstattributes.h"
#include "vstgui/plugin-bindings/vst3editor.h"

#include <krate/dsp/core/note_value.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>

// Compile touch point for the update config (FR-052, plan section 2.6): the
// header is included by nothing else in the plugin, and a .h in a CMake source
// list is HEADER_FILE_ONLY, so this static_assert is what compiles it.
// No UpdateChecker is ever instantiated (FR-052).
#include "update/profundum_update_config.h"
static_assert(std::is_same_v<decltype(Profundum::makeProfundumUpdateConfig()),
                             Krate::Plugins::UpdateCheckerConfig>);

namespace Profundum {

using namespace Steinberg;
using namespace Steinberg::Vst;

tresult PLUGIN_API Controller::initialize(FUnknown* context) {
    const tresult result = EditControllerEx1::initialize(context);
    if (result != kResultOk) {
        return result;
    }

    // FR-001 (Phase 11 T014): 8 Phase 10 + 4 performance + 60 sound + 8 note + 4 morph control = 84.
    registerGlobalParams(parameters);
    registerMacroParams(parameters);
    registerPerformanceParams(parameters);
    registerSoundParams(parameters);
    registerNoteParams(parameters);
    registerMorphControlParams(parameters);

    // FR-050. No UpdateChecker (FR-052).
    presetManager_ = std::make_unique<Krate::Plugins::PresetManager>(makeProfundumPresetConfig(),
                                                                     nullptr, this);
    return kResultOk;
}

tresult PLUGIN_API Controller::terminate() {
    presetManager_ = nullptr;
    return EditControllerEx1::terminate();
}

// FR-045 / FR-046 (plan 4.6 setComponentState steps 1-6): the processor's stream mirrored into the parameters
// and the endpoint mirror. A future version is refused before any field is read; the packs sanitise each field
// before they normalise it and skip the call for an invalid one.
//  - v1 (FR-044 reset): the mirror becomes MorphState{} for both endpoints and 410-417, 105, 1000-1002 return to
//    their registered defaults (1001 to A).
//  - v2 (FR-044 keep): note block -> morph-control block (105 through sanitizeMorphPosition) -> the seed (skipped)
//    -> endpoint A -> endpoint B; the 20-byte runtime blob is not read. A short read leaves the stream at its end,
//    so every later read fails too and every later field keeps its prior value (the chain stops at the cut). A cut
//    endpoint (fewer than 201 bytes) or a rejected one (deserializeMorphState false) keeps its prior mirror.
//  - always: the hidden performance IDs return to 0.5 / 0 / 0 / 0, then the Edit Target is re-displayed.
// inStateLoad_ is held for the whole load, so the 1001 hook neither re-displays nor calls restartComponent: the host
// is loading state. Nothing here calls beginEdit / performEdit / endEdit.
tresult PLUGIN_API Controller::setComponentState(IBStream* state) {
    if (state == nullptr) {
        return kResultFalse;
    }
    IBStreamer s(state, kLittleEndian);
    int32 version = 0;
    if (!s.readInt32(version)) {
        return kResultFalse;
    }
    if (version > kCurrentStateVersion) {
        return kResultFalse;
    }
    inStateLoad_ = true;
    const auto set = [this](ParamID id, double v) { setParamNormalized(id, v); };
    loadGlobalParamsToController(s, set);
    loadMacroParamsToController(s, set);

    if (version == kStateV1Version) {
        mirror_ = {MorphState{}, MorphState{}};
        for (ParamID id = kNoteMonoPriorityId; id <= kNotePitchBendRangeId; ++id) {
            setToRegisteredDefault(id);
        }
        setToRegisteredDefault(kMorphPositionId);
        setToRegisteredDefault(kMorphAnchorModeId);
        setToRegisteredDefault(kMorphEditTargetId);
        setToRegisteredDefault(kMorphRampTimeId);
    } else if (version >= kCurrentStateVersion) {
        loadNoteParamsToController(s, set);
        loadMorphControlParamsToController(s, set);
        uint32 seed = 0;  // FR-043: the seed is the processor's; the controller skips it
        if (s.readInt32u(seed)) {
            std::array<std::byte, Krate::DSP::kMorphStateBytes> buf{};
            for (MorphState& slot : mirror_) {
                if (s.readRaw(buf.data(), static_cast<TSize>(buf.size())) != static_cast<TSize>(buf.size())) {
                    break;  // a cut endpoint keeps its prior mirror and stops the chain
                }
                MorphState local{};
                if (Krate::DSP::deserializeMorphState(buf.data(), buf.size(), local)) {
                    slot = local;  // a rejected endpoint keeps its prior mirror; the chain continues
                }
            }
        }
    }

    // FR-046 (plan 4.6 step 5): the hidden performance parameters are never persisted.
    EditControllerEx1::setParamNormalized(kPitchBendId, static_cast<ParamValue>(kDefaultPitchBendNorm));
    EditControllerEx1::setParamNormalized(kSustainPedalId, static_cast<ParamValue>(kDefaultSustainNorm));
    EditControllerEx1::setParamNormalized(kModWheelId, static_cast<ParamValue>(kDefaultModWheel));
    EditControllerEx1::setParamNormalized(kChannelPressureId, static_cast<ParamValue>(kDefaultChannelPressure));
    inStateLoad_ = false;

    redisplayEditTarget();  // no restartComponent: the host is loading state
    return kResultOk;
}

// FR-044 v1 reset: the parameter's registered defaultNormalizedValue (through the 1001 hook, guarded by inStateLoad_).
void Controller::setToRegisteredDefault(ParamID id) {
    if (Parameter* p = getParameterObject(id)) {
        setParamNormalized(id, p->getInfo().defaultNormalizedValue);
    }
}

tresult PLUGIN_API Controller::getParamStringByValue(ParamID tag, ParamValue valueNormalized,
                                                     String128 string) {
    // Each pack formats only its own continuous IDs (kResultFalse otherwise).
    if (formatGlobalParam(tag, valueNormalized, string) == kResultOk) {
        return kResultOk;
    }
    if (formatMacroParam(tag, valueNormalized, string) == kResultOk) {
        return kResultOk;
    }
    if (formatSoundParam(tag, valueNormalized, string) == kResultOk) {
        return kResultOk;
    }
    // FR-007: the Portamento (412) suffix follows the CURRENT Glide Mode (414).
    const int glideModeIndex = indexFromNormalized(getParamNormalized(kNoteGlideModeId), kMaxGlideModeIndex);
    if (formatNoteParam(tag, valueNormalized, glideModeIndex, string) == kResultOk) {
        return kResultOk;
    }
    if (formatMorphControlParam(tag, valueNormalized, string) == kResultOk) {
        return kResultOk;
    }
    if (formatPerformanceParam(tag, valueNormalized, string) == kResultOk) {
        return kResultOk;
    }
    // Every list (Voice Mode, Polyphony, the list rows, 410-416, 1000, 1001) is a StringListParameter and
    // Pitch Bend Range (417) a RangeParameter: they format themselves.
    return EditControllerEx1::getParamStringByValue(tag, valueNormalized, string);
}

// FR-022 / FR-024 (plan 4.6): an Edit Target switch re-displays the 60 E-parameters from the selected
// mirror, then asks the host to re-read its cached values once. No beginEdit / performEdit / endEdit: nothing
// is recorded as automation. A same-index write, or one during setComponentState, is not a switch.
tresult PLUGIN_API Controller::setParamNormalized(ParamID tag, ParamValue value) {
    if (tag != kMorphEditTargetId) {
        return EditControllerEx1::setParamNormalized(tag, value);
    }
    const int before = editTargetIndex();
    const tresult result = EditControllerEx1::setParamNormalized(tag, value);
    if (result != kResultTrue || inStateLoad_ || editTargetIndex() == before) {
        return result;
    }
    // FR-025 pull (ruling 2026-10-22): ask the processor for its slots BEFORE re-displaying. A host that delivers
    // the reply at once has replaced the mirror by the time sendMessageID returns; a later reply re-displays and
    // restarts again (notify). Armed before the send, because a synchronous reply ends (kMsgCompareMode) inside it.
    pullRedisplayed_ = false;
    pullAwaitingReply_ = true;
    if (sendMessageID(kMsgRequestSync) != kResultOk) {
        pullAwaitingReply_ = false;  // no host context or no peer: nothing will answer
    }
    redisplayEditTarget();
    pullRedisplayed_ = true;
    if (componentHandler) {
        componentHandler->restartComponent(kParamValuesChanged);
    }
    return result;
}

// FR-030 (plan 4.6): the request only; the mirror changes when the processor reports the destination endpoint.
void Controller::copyEndpoint(Krate::DSP::BassMorphEngine::Endpoint from) {
    IPtr<IMessage> msg = owned(allocateMessage());
    IAttributeList* attrs = (msg.get() != nullptr) ? msg->getAttributes() : nullptr;
    if (attrs != nullptr) {
        msg->setMessageID(kMsgCopyEndpoint);
        attrs->setInt("from", static_cast<int64>(from));
        static_cast<void>(sendMessage(msg));
    }
    static_cast<void>(setDirty(1u));  // vsteditcontroller.h:91 (IComponentHandler2, when the host offers it)
}

// FR-031 (plan 4.6): the request only; compare_ changes when the processor's kMsgCompareMode reply arrives.
void Controller::setCompare(CompareMode m) {
    IPtr<IMessage> msg = owned(allocateMessage());
    IAttributeList* attrs = (msg.get() != nullptr) ? msg->getAttributes() : nullptr;
    if (attrs == nullptr) {
        return;
    }
    msg->setMessageID(kMsgCompare);
    attrs->setInt("mode", static_cast<int64>(static_cast<std::uint8_t>(m)));
    static_cast<void>(sendMessage(msg));
}

int Controller::editTargetIndex() {
    return indexFromNormalized(getParamNormalized(kMorphEditTargetId), kNumEditTargets - 1);
}

// FR-022: the base-class setParamNormalized, which does not notify the host. Sync Rate pairs display
// syncRateIndexOf / 29 (FR-012); every other row normalizedFromLeafWord of its leaf.
void Controller::redisplayEditTarget() {
    const LeafBits bits = leafBitsOf(mirror_[static_cast<std::size_t>(editTargetIndex())]);
    for (const SoundParamRow& row : kSoundRows) {
        double v = 0.0;
        if (row.leaf2 != kNoLeaf) {
            const int idx = syncRateIndexOf(bits[row.leaf], bits[row.leaf2]);
            if (idx < 0) {
                continue;  // a pair in no dropdown entry keeps its displayed value
            }
            v = static_cast<double>(idx) / static_cast<double>(Krate::DSP::kNoteValueDropdownCount - 1);
        } else {
            v = normalizedFromLeafWord(row.leaf, bits[row.leaf]);
        }
        EditControllerEx1::setParamNormalized(row.id, v);
    }
    ++redisplayCount_;
}

// FR-025 / FR-021 (plan 4.6): the processor is authoritative. kMsgEndpoint replaces one mirror from its
// 201-byte blob (a rejected blob leaves it unchanged) and re-displays when it is the Edit Target, with no
// restart and no performEdit. kMsgCompareMode sets the compare mode. Every other message -> the base class.
tresult PLUGIN_API Controller::notify(IMessage* message) {
    if (message == nullptr) {
        return kInvalidArgument;
    }
    const FIDString id = message->getMessageID();
    if (FIDStringsEqual(id, kMsgEndpoint)) {
        IAttributeList* attrs = message->getAttributes();
        if (attrs == nullptr) {
            return kResultFalse;
        }
        int64 endpoint = -1;
        const void* data = nullptr;
        uint32 size = 0;
        if (attrs->getInt("endpoint", endpoint) != kResultOk || endpoint < 0 || endpoint >= kNumEditTargets ||
            attrs->getBinary("blob", data, size) != kResultOk || size != Krate::DSP::kMorphStateBytes) {
            return kResultFalse;
        }
        MorphState local{};
        if (!Krate::DSP::deserializeMorphState(static_cast<const std::byte*>(data), size, local)) {
            return kResultFalse;
        }
        mirror_[static_cast<std::size_t>(endpoint)] = local;
        if (endpoint == editTargetIndex()) {
            redisplayEditTarget();
            // FR-025 / FR-024: a pull reply that lands after the switch's re-display -> the host re-reads again.
            if (pullAwaitingReply_ && pullRedisplayed_ && componentHandler) {
                componentHandler->restartComponent(kParamValuesChanged);
            }
        }
        return kResultOk;
    }
    if (FIDStringsEqual(id, kMsgCompareMode)) {
        IAttributeList* attrs = message->getAttributes();
        int64 mode = -1;
        if (attrs == nullptr || attrs->getInt("mode", mode) != kResultOk || mode < 0 ||
            mode > static_cast<int64>(CompareMode::B)) {
            return kResultFalse;
        }
        compare_ = static_cast<CompareMode>(mode);
        pullAwaitingReply_ = false;  // the sender reports the compare mode last: a pull reply is complete
        pullRedisplayed_ = false;
        return kResultOk;
    }
    return EditControllerEx1::notify(message);
}

// FR-055 (model: plugins/vorago/src/controller/controller.cpp:271-278).
// FR-050 (Vorago controller.cpp:280-296 precedent). Bus 0 only; every channel maps the same four controllers to the
// hidden performance parameters.
tresult PLUGIN_API Controller::getMidiControllerAssignment(int32 busIndex, int16 /*channel*/,
                                                           CtrlNumber midiControllerNumber, ParamID& id) {
    if (busIndex != 0) {
        return kResultFalse;
    }
    switch (midiControllerNumber) {
        case kCtrlModWheel:
            id = kModWheelId;
            return kResultOk;
        case kCtrlSustainOnOff:
            id = kSustainPedalId;
            return kResultOk;
        case kAfterTouch:
            id = kChannelPressureId;
            return kResultOk;
        case kPitchBend:
            id = kPitchBendId;
            return kResultOk;
        default:
            return kResultFalse;
    }
}

IPlugView* PLUGIN_API Controller::createView(FIDString name) {
    if (FIDStringsEqual(name, Vst::ViewType::kEditor)) {
        return new VSTGUI::VST3Editor(this, "editor", "editor.uidesc");
    }
    return nullptr;
}

}  // namespace Profundum
