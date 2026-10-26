// ==============================================================================
// Profundum - Edit Controller implementation   Plan section 2.6
// ==============================================================================
// T010: initialize registers the eight Phase 10 parameters (global pack, then
// macro pack) and getParamStringByValue formats the continuous ones (FR-047).
// T012: setComponentState mirrors the processor stream (FR-045, FR-046).
// T019: the PresetManager (FR-050) and createView on the stock-view placeholder
// editor.uidesc (FR-054, FR-055).
// Phase 11 T014: initialize registers all 84 parameters (global, macro,
// performance, sound, note, morph control) and getParamStringByValue formats
// every continuous one (FR-001 - FR-009).
// Phase 11 T016: the endpoint mirror (FR-021), the Edit Target re-display and
// restart (FR-022, FR-024) and the endpoint / compare-mode notify (FR-025).
// Phase 11 T019: setComponentState parses state v1 and v2 (FR-044, FR-045).
// Phase 11 T021: IMidiMapping::getMidiControllerAssignment (FR-050).
// Phase 12 T019: state v3 endpoint names (FR-047, FR-096) and setEndpointName (kMsgEndpointName).
// Phase 12 T021: IDataExchangeReceiver - the cached ProfundumSpectrumFrame (FR-071, FR-076, FR-091; plan 4.6.1).
// Phase 12 T023: macroRingRange / refreshMacroRings (FR-061, P-5) and endpointCaption (FR-040, FR-048).
// Phase 12 T024: refreshSpreadModel, the SPREAD model of the controller (FR-080, SC-014; plan 4.6.8).
// Phase 12 T025: wireAdsrDisplay / adsrToRegistered, the ADSRDisplay law adapter (FR-034, SC-024; plan 4.6.10).
// Phase 12 T026: VST3EditorDelegate (custom views, "ProfundumPanel"), the editor sync timer, the gesture count, the
// session zoom / page / tab and the panel refresh hooks (FR-013, FR-014, FR-035, FR-044, FR-090 - FR-092; plan 4.6.2,
// 4.6.4, 4.6.7, 4.6.9). D-11 / P-4: the shared view creators are registered by the ui/*.h includes below, so the test
// binary, which never compiles entry.cpp, builds the same view tree as the plugin.
// Phase 13 T013: the PresetManager state / load providers, loadPresetStream (kMsgLoadPresetState, then the mirror),
// the PresetBrowserView overlay and the "PresetBrowserButton" custom view (FR-030, FR-031, FR-032, FR-059; plan 4.6).
// ==============================================================================

#include "controller/controller.h"

#include "parameters/endpoint_names.h"
#include "parameters/global_params.h"
#include "parameters/macro_params.h"
#include "parameters/morph_params.h"
#include "parameters/note_params.h"
#include "parameters/performance_params.h"
#include "parameters/sound_param_row.h"
#include "parameters/sound_param_table.h"
#include "plugin_ids.h"
#include "preset/profundum_preset_config.h"
#include "ui/endpoint_descriptor.h"
#include "ui/macro_ui_map.h"
#include "ui/offset_ring_knob.h"  // also registers the "OffsetRingKnob" view creator in this TU (plan 4.13)
#include "ui/adsr_display.h"      // plugins/shared/src/ui (the T025 adapter; also registers "ADSRDisplay")
#include "ui/arc_knob.h"          // plugins/shared/src/ui: registers "ArcKnob" (D-11)
#include "ui/bipolar_slider.h"    // plugins/shared/src/ui: registers "BipolarSlider" (D-11)
#include "ui/harmonic_bar_view.h"
#include "ui/outline_button.h"       // plugins/shared/src/ui: OutlineBrowserButton (Phase 13 FR-059)
#include "ui/panel_sub_controller.h"
#include "ui/preset_browser_view.h"  // plugins/shared/src/ui (Phase 13 FR-030)
#include "ui/spread_curve_view.h"
#include "ui/update_banner_view.h"  // plugins/shared/src/ui: VersionLabel, UpdateBannerView, CheckForUpdatesButton
#include "version.h"

#include "pluginterfaces/vst/ivstattributes.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "public.sdk/source/common/memorystream.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cpoint.h"
#include "vstgui/lib/crect.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "vstgui/uidescription/uiattributes.h"

#include <krate/dsp/core/note_value.h>
#include <krate/dsp/primitives/envelope_utils.h>
#include <krate/dsp/systems/bass_stereo_field.h>
#include <krate/dsp/systems/profundum_macro_matrix.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The live update check (2026-10-26): the FR-052 "compiled but unused" ruling is superseded, as Vorago's was on
// 2026-10-11; the controller owns an UpdateChecker like every plugin with a live check.
#include "update/profundum_update_config.h"

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

    // FR-050.
    presetManager_ = std::make_unique<Krate::Plugins::PresetManager>(makeProfundumPresetConfig(),
                                                                     nullptr, this);
    // Phase 13 FR-031 (plan 4.6, Vorago controller.cpp:113-118): save the processor's own getState bytes; load the
    // whole stream through the processor first (loadPresetStream).
    presetManager_->setStateProvider([this]() -> IBStream* { return createComponentStateStream(); });
    presetManager_->setLoadProvider(
        [this](IBStream* stream, const Krate::Plugins::PresetInfo& /*info*/) { return loadPresetStream(stream); });
    presetTabLabels_ = makeProfundumPresetTabLabels();
    updateChecker_ = std::make_unique<Krate::Plugins::UpdateChecker>(makeProfundumUpdateConfig());

    // Phase 12 FR-080 (plan 4.6.8): the never-prepared field the SPREAD model evaluates, then the model of the
    // registered defaults.
    field_ = std::make_unique<Krate::DSP::BassStereoField>();
    refreshSpreadModel();
    return kResultOk;
}

tresult PLUGIN_API Controller::terminate() {
    stopEditorSyncTimer();  // a host that tears down without willClose must not leave the timer firing
    updateChecker_.reset();
    presetManager_ = nullptr;
    return EditControllerEx1::terminate();
}

// FR-045 / FR-046 (plan 4.6 setComponentState steps 1-6): the processor's stream mirrored into the parameters
// and the endpoint mirror. A future version is refused before any field is read; the packs sanitise each field
// before they normalise it and skip the call for an invalid one.
//  - v1 (FR-044 reset): the mirror becomes MorphState{} for both endpoints and 410-417, 105, 1000-1002 return to
//    their registered defaults (1001 to A).
//  - v2 (FR-044 keep): note block -> morph-control block (105 through sanitizeMorphPosition) -> the seed (skipped)
//    -> endpoint A -> endpoint B; the 20-byte runtime blob is skipped, not decoded (v3: the two name blocks follow
//    it, Phase 12 T019; v2: the names become "A" / "B"). A short read leaves the stream at its end,
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
        resetNamesToDefaults();  // Phase 12 FR-047: a v1 stream has no names
    } else if (version >= kStateV2Version) {  // Phase 12 T019 (R-11): v2 AND v3, never only the current version
        loadNoteParamsToController(s, set);
        loadMorphControlParamsToController(s, set);
        uint32 seed = 0;  // FR-043: the seed is the processor's; the controller skips it
        bool chainIntact = false;
        if (s.readInt32u(seed)) {
            chainIntact = true;
            std::array<std::byte, Krate::DSP::kMorphStateBytes> buf{};
            for (MorphState& slot : mirror_) {
                if (s.readRaw(buf.data(), static_cast<TSize>(buf.size())) != static_cast<TSize>(buf.size())) {
                    chainIntact = false;
                    break;  // a cut endpoint keeps its prior mirror and stops the chain
                }
                MorphState local{};
                if (Krate::DSP::deserializeMorphState(buf.data(), buf.size(), local)) {
                    slot = local;  // a rejected endpoint keeps its prior mirror; the chain continues
                }
            }
        }
        if (version == kStateV2Version) {
            resetNamesToDefaults();  // Phase 12 FR-047: a v2 stream has no names
        } else if (chainIntact) {
            // v3 (plan 4.5): skip the 20-byte runtime blob (the processor's), then read both name blocks. A short or
            // invalid block keeps that endpoint's prior name and stops the chain; an empty name is the default.
            std::array<std::byte, Krate::DSP::BassMorphEngine::kMorphRuntimeBytes> runtime{};
            if (s.readRaw(runtime.data(), static_cast<TSize>(runtime.size())) == static_cast<TSize>(runtime.size())) {
                for (std::size_t e = 0; e < names_.size(); ++e) {
                    EndpointName name{};
                    if (!readEndpointName(s, name)) {
                        break;
                    }
                    const auto endpoint =
                        e == 1u ? Krate::DSP::BassMorphEngine::Endpoint::B : Krate::DSP::BassMorphEngine::Endpoint::A;
                    names_[e] = name.length == 0u ? defaultEndpointName(endpoint) : name;
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

// Phase 13 FR-031 / FR-032 (plan 4.6 "Controller"): the processor is authoritative, so it receives the whole stream
// first (kMsgLoadPresetState; its setState does the restore hand-off, the names and the controller sync); the mirror
// then reads the same bytes. Nothing changes here when the message cannot be sent or the processor refuses it.
bool Controller::loadPresetStream(IBStream* stream) {
    if (stream == nullptr) {
        return false;
    }
    constexpr std::size_t kMaxPresetStateBytes = 4096;  // the processor's kMsgLoadPresetState bound
    // One byte more than the bound, so an oversize chunk is seen without reading all of it.
    std::vector<std::byte> bytes(kMaxPresetStateBytes + 1u);
    std::size_t total = 0;
    while (total < bytes.size()) {
        int32 got = 0;
        const tresult r = stream->read(bytes.data() + total, static_cast<int32>(bytes.size() - total), &got);
        if (got > 0) {
            total += static_cast<std::size_t>(got);  // a short read (FileStream: kResultFalse) still delivered bytes
        }
        if (r != kResultOk || got <= 0) {
            break;
        }
    }
    if (total < sizeof(int32) || total > kMaxPresetStateBytes) {
        return false;
    }
    bytes.resize(total);
    std::uint32_t versionWord = 0;  // the state is little-endian (IBStreamer kLittleEndian)
    for (std::size_t i = 0; i < sizeof(versionWord); ++i) {
        versionWord |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[i])) << (8u * i);
    }
    if (static_cast<int32>(versionWord) > kCurrentStateVersion) {
        return false;
    }

    // 1. The processor first. No host context -> allocateMessage() is nullptr -> refused, mirror untouched.
    IPtr<IMessage> msg = owned(allocateMessage());
    IAttributeList* attrs = (msg.get() != nullptr) ? msg->getAttributes() : nullptr;
    if (attrs == nullptr) {
        return false;
    }
    msg->setMessageID(kMsgLoadPresetState);
    if (attrs->setBinary("state", bytes.data(), static_cast<uint32>(bytes.size())) != kResultOk) {
        return false;
    }
    if (sendMessage(msg) != kResultOk) {
        return false;  // no peer, or the processor refused the stream: the mirror never runs ahead of it
    }

    // 2. The mirror, from the same bytes.
    auto mirrorStream = owned(new MemoryStream());
    int32 written = 0;
    if (mirrorStream->write(bytes.data(), static_cast<int32>(bytes.size()), &written) != kResultOk ||
        std::cmp_not_equal(written, bytes.size()) ||
        mirrorStream->seek(0, IBStream::kIBSeekSet, nullptr) != kResultOk) {
        return false;
    }
    const tresult mirrored = setComponentState(mirrorStream);

    // 3. The host re-reads every parameter once.
    if (componentHandler) {
        componentHandler->restartComponent(kParamValuesChanged);
    }
    return mirrored == kResultOk;
}

// Phase 13 FR-031 (Vorago controller.cpp:152-168; Ruinae controller_presets.cpp:372-385): delegate to the processor
// through the host's IComponent::getState, never re-serialise from the controller.
IBStream* Controller::createComponentStateStream() {
    FUnknownPtr<IComponent> component(getComponentHandler());
    if (!component) {
        return nullptr;
    }
    auto* stream = new MemoryStream();
    if (component->getState(stream) != kResultOk) {
        stream->release();
        return nullptr;
    }
    stream->seek(0, IBStream::kIBSeekSet, nullptr);
    return stream;
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
    // Echo guard (see performEdit): the tag being set, restored on every return (a nested set of another ID).
    struct ParamBeingSet {
        ParamID* slot;
        ParamID previous;
        ~ParamBeingSet() { *slot = previous; }
    };
    const ParamBeingSet guard{.slot = &paramBeingSet_, .previous = std::exchange(paramBeingSet_, tag)};
    if (tag != kMorphEditTargetId) {
        const tresult result = EditControllerEx1::setParamNormalized(tag, value);
        // Phase 12 FR-061 (plan 4.6.4): a macro or a macro target moved -> the rings (no-op with no editor open).
        if (isMacroRingTrigger(tag)) {
            refreshMacroRings();
        }
        // Phase 12 FR-080 (plan 4.6.4): a field control of the SPREAD curve moved -> the model.
        if (isSpreadTrigger(tag)) {
            refreshSpreadModel();
        }
        // Phase 12 FR-034 (plan 4.6.10): the reverse path of the ADSRDisplay adapter (no-op while none is wired).
        if (isAdsrParam(tag)) {
            syncAdsrDisplay(tag);
        }
        // Phase 12 plan 4.6.4 (T026): the panel hooks, only while an editor (and so the panel) exists.
        if (panel_ != nullptr) {
            if (tag == kNoteGlideModeId) {
                panel_->refreshPortamentoReadout();  // FR-035: the 412 readout re-formats with the new suffix
            } else if (tag == kMorphAnchorModeId) {
                panel_->refreshAnchorDimming();  // FR-044
            }
        }
        return result;
    }
    const int before = editTargetIndex();
    const tresult result = EditControllerEx1::setParamNormalized(tag, value);
    if (panel_ != nullptr) {
        panel_->refreshAnchorDimming();  // FR-044: 1001 is the other half of the Hold-and-B condition
    }
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

// Phase 12 FR-049 (plan 4.6.3): one kMsgCompare; compare A / B also switches Edit Target 1001 through its gesture
// (FR-043), which fires the 1001 hook in setParamNormalized. A.B leaves 1001 unchanged. compare_ still changes
// only from the processor's reply.
void Controller::pressCompare(CompareMode m) {
    setCompare(m);
    if (m == CompareMode::A || m == CompareMode::B) {
        const double v = (m == CompareMode::B) ? 1.0 : 0.0;
        static_cast<void>(beginEdit(kMorphEditTargetId));
        static_cast<void>(performEdit(kMorphEditTargetId, v));
        static_cast<void>(setParamNormalized(kMorphEditTargetId, v));
        static_cast<void>(endEdit(kMorphEditTargetId));
    }
}

// Phase 12 FR-047 (plan 4.5): sanitise, store (the UI shows it at once), send kMsgEndpointName, setDirty(true).
// Without a host context allocateMessage() returns nullptr and nothing is sent; the name is still stored.
// The morph component refresh (plan 4.5 step 5) arrives with the component itself (T022).
void Controller::setEndpointName(Krate::DSP::BassMorphEngine::Endpoint e, std::string_view text) {
    const std::size_t idx = e == Krate::DSP::BassMorphEngine::Endpoint::B ? 1u : 0u;
    EndpointName name = sanitizeEndpointName(text);
    if (name.length == 0u) {
        name = defaultEndpointName(e);  // plan 4.5: an empty result means the default name
    }
    names_[idx] = name;
    refreshEndpointCaptions();  // plan 4.6.6: every name change
    IPtr<IMessage> msg = owned(allocateMessage());
    IAttributeList* attrs = (msg.get() != nullptr) ? msg->getAttributes() : nullptr;
    if (attrs != nullptr) {
        msg->setMessageID(kMsgEndpointName);
        attrs->setInt("endpoint", static_cast<int64>(idx));
        const std::string_view bytes = name.view();
        attrs->setBinary("name", bytes.data(), static_cast<uint32>(bytes.size()));
        static_cast<void>(sendMessage(msg));
    }
    static_cast<void>(setDirty(1u));  // vsteditcontroller.h:91 (IComponentHandler2, when the host offers it)
}

// FR-047: a v1 or v2 stream carries no names (setComponentState only).
void Controller::resetNamesToDefaults() {
    names_[0] = defaultEndpointName(Krate::DSP::BassMorphEngine::Endpoint::A);
    names_[1] = defaultEndpointName(Krate::DSP::BassMorphEngine::Endpoint::B);
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
    refreshMacroRings();  // plan 4.6.4: the base setParamNormalized above bypasses the override's ring hook
    refreshSpreadModel();  // ... and its SPREAD hook (800 - 805)
    refreshAdsrDisplay();  // ... and its ADSRDisplay hook (420 - 426, plan 4.6.10 re-display)
    refreshEndpointCaptions();  // plan 4.6.4 / 4.6.6
}

// FR-025 / FR-021 (plan 4.6): the processor is authoritative. kMsgEndpoint replaces one mirror from its
// 201-byte blob (a rejected blob leaves it unchanged) and re-displays when it is the Edit Target, with no
// restart and no performEdit. kMsgCompareMode sets the compare mode. Every other message -> the base class.
tresult PLUGIN_API Controller::notify(IMessage* message) {
    if (message == nullptr) {
        return kInvalidArgument;
    }
    // Phase 12 FR-076 (plan 4.6.1): the SDK IMessage fallback ("DataExchange", "DataExchangeQueueOpened",
    // "DataExchangeQueueClosed") is decoded into the IDataExchangeReceiver methods below, before the Phase 11 dispatch.
    if (dataExchangeReceiver_.onMessage(message)) {
        return kResultOk;
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
        refreshEndpointCaptions();  // plan 4.6.6: every mirror replacement, Edit Target or not
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
        ++compareChanges_;  // Phase 12 FR-046: one change notification per accepted message (SC-007 (b))
        if (panel_ != nullptr) {
            panel_->onCompareModeChanged(compare_);  // plan 4.6.3: the compare segments show the reply
        }
        pullAwaitingReply_ = false;  // the sender reports the compare mode last: a pull reply is complete
        pullRedisplayed_ = false;
        return kResultOk;
    }
    return EditControllerEx1::notify(message);
}

// ==============================================================================
// Phase 12 T023 - macro rings (FR-061; plan 4.6.5, 5.5; P-5 ruling) and endpoint captions (FR-040, FR-048; 4.6.6)
// ==============================================================================

namespace {

constexpr std::size_t kNoMacroTarget = Krate::DSP::ProfundumMacroMatrix::kNumTargets;

/// The ProfundumMacroTarget index whose registered control is @p id (macro_ui_map.h), or kNoMacroTarget.
[[nodiscard]] std::size_t macroTargetIndexOf(ParamID id) noexcept {
    if (id == UI::kNoParamId) {
        return kNoMacroTarget;
    }
    for (std::size_t t = 0; t < UI::kMacroTargetParamIds.size(); ++t) {
        if (UI::kMacroTargetParamIds[t] == id) {
            return t;
        }
    }
    return kNoMacroTarget;
}

/// The amp times move in the log domain (profundum_macro_matrix.h:216-218, :259-268).
[[nodiscard]] bool isAmpTimeTarget(Krate::DSP::ProfundumMacroTarget t) noexcept {
    using T = Krate::DSP::ProfundumMacroTarget;
    return t == T::AmpAttackOct || t == T::AmpDecayOct || t == T::AmpReleaseOct;
}

}  // namespace

// Plan 4.6.4: 100 - 103 and every registered macro target refresh the rings.
bool Controller::isMacroRingTrigger(ParamID tag) noexcept {
    return (tag >= kMacroWeightId && tag <= kMacroCleanViolentId) || macroTargetIndexOf(tag) != kNoMacroTarget;
}

// Plan 4.6.5 steps 1 - 4: the engine's own contributions() at the current macros, the engine's apply rule on the
// target's displayed plain value, then the signed normalised delta. Nothing is written.
float Controller::macroRingRange(ParamID id) noexcept {
    using Krate::DSP::ProfundumMacroMatrix;
    using Krate::DSP::ProfundumMacroTarget;
    const std::size_t t = macroTargetIndexOf(id);
    if (t == kNoMacroTarget) {
        return 0.0f;
    }
    const auto& macroRow = ProfundumMacroMatrix::kRows[t];  // kRows[t].target == t (macro_ui_map.h static_assert)
    if (macroRow.target == ProfundumMacroTarget::CompressionAmount || macroRow.amount == 0.0f) {
        return 0.0f;
    }
    const SoundParamRow* row = soundRowOf(id);
    if (row == nullptr) {
        return 0.0f;
    }

    // Copy-then-assign: getParamNormalized is double, the members are float (no narrowing brace init, R-10).
    Krate::DSP::ProfundumMacroValues v{};
    v.weight = static_cast<float>(getParamNormalized(kMacroWeightId));
    v.darkBright = static_cast<float>(getParamNormalized(kMacroDarkBrightId));
    v.tightLoose = static_cast<float>(getParamNormalized(kMacroTightLooseId));
    v.cleanViolent = static_cast<float>(getParamNormalized(kMacroCleanViolentId));
    ProfundumMacroMatrix mm;
    mm.setMacros(v);
    const float c = mm.contributions()[t];
    if (c == 0.0f) {  // +0 and -0: the neutral draws no ring
        return 0.0f;
    }

    const FieldInfo& f = lawOf(row->leaf);
    const double base = plainFromNormalized(f, getParamNormalized(id));
    const double mod = isAmpTimeTarget(macroRow.target)
                           ? std::clamp(base * std::exp2(static_cast<double>(c)),
                                        static_cast<double>(Krate::DSP::kMinEnvelopeTimeMs),
                                        static_cast<double>(Krate::DSP::kMaxEnvelopeTimeMs))
                           : std::clamp(base + static_cast<double>(c), static_cast<double>(f.lo),
                                        static_cast<double>(f.hi));
    return static_cast<float>(normalizedFromPlain(f, mod) - normalizedFromPlain(f, base));
}

void Controller::refreshMacroRings() noexcept {
    for (std::size_t t = 0; t < ringKnobs_.size(); ++t) {
        for (OffsetRingKnob* knob : ringKnobs_[t]) {
            if (knob != nullptr) {
                knob->setOffset(macroRingRange(UI::kMacroTargetParamIds[t]));
            }
        }
    }
}

void Controller::registerRingKnob(ParamID id, OffsetRingKnob* knob) noexcept {
    const std::size_t t = macroTargetIndexOf(id);
    if (t == kNoMacroTarget) {
        return;
    }
    if (knob == nullptr) {
        return;
    }
    auto& slots = ringKnobs_[t];
    if (std::find(slots.begin(), slots.end(), knob) == slots.end()) {
        const auto slot = std::find(slots.begin(), slots.end(), nullptr);
        if (slot == slots.end()) {
            return;  // more knobs than kMaxRingKnobsPerTarget: the uidesc binds at most 3 (T029 KnobsCarryRing)
        }
        *slot = knob;
    }
    knob->setOffset(macroRingRange(id));
}

// Plan 4.6.6: "<name> -- w1 . w2 . w3" with an em dash (U+2014) and middle dots (U+00B7) as UTF-8 bytes, from the
// current name and mirror.
std::string Controller::endpointCaption(Krate::DSP::BassMorphEngine::Endpoint e) const {
    const std::size_t idx = e == Krate::DSP::BassMorphEngine::Endpoint::B ? 1u : 0u;
    const auto words = UI::describeEndpoint(mirror_[idx]);
    std::string caption(names_[idx].view());
    caption += " \xE2\x80\x94 ";
    caption += words[0];
    caption += " \xC2\xB7 ";
    caption += words[1];
    caption += " \xC2\xB7 ";
    caption += words[2];
    return caption;
}

// ==============================================================================
// Phase 12 T024 - SPREAD model (FR-080, SC-014; plan 4.6.8)
// ==============================================================================

// Plan 4.6.4: the six field controls the static width curve reads (806 - 809 do not move it).
bool Controller::isSpreadTrigger(ParamID tag) noexcept {
    return tag >= kFieldImageModeId && tag <= kFieldMotionDepthId;
}

// Plan 4.6.8 steps 1 - 6. The field is never prepared: setControls latches, reset() applies the latch, snaps the
// law crossover and rebuilds the law cache (bass_stereo_field.h:196-228), so widthCurve is c(f) of these controls.
void Controller::refreshSpreadModel() noexcept {
    using Field = Krate::DSP::BassStereoField;
    if (field_ == nullptr) {
        return;
    }
    // Step 1: the displayed plain values, each through its row's law.
    const auto plainOf = [this](ParamID id) -> double {
        const SoundParamRow* row = soundRowOf(id);
        return row != nullptr ? plainFromNormalized(lawOf(row->leaf), getParamNormalized(id)) : 0.0;
    };
    // Step 2: copy-then-assign (double -> float would narrow in a brace init, R-10).
    Field::Controls c{};
    c.mode = static_cast<Field::ImageMode>(static_cast<std::uint8_t>(plainOf(kFieldImageModeId)));
    c.spread = static_cast<float>(plainOf(kFieldSpreadId));
    c.gravity = static_cast<float>(plainOf(kFieldGravityId));
    c.crossoverHz = static_cast<float>(plainOf(kFieldCrossoverId));
    c.curve = static_cast<float>(plainOf(kFieldCurveId));
    c.motionDepth = static_cast<float>(plainOf(kFieldMotionDepthId));
    // Step 3.
    field_->setControls(c);
    (*field_).reset();  // the field's reset, not the unique_ptr's
    const Field::Controls& a = field_->controls();  // the sanitised set reset() applied
    // Step 4: m with m_mot = 0 (the Motion excursion band is not drawn).
    const bool solid = a.mode == Field::ImageMode::Solid;
    const float m = solid ? 0.0f : a.spread * (1.0f - Field::kMotionExcursion * a.motionDepth);
    // Step 5.
    for (std::size_t i = 0; i < spreadModel_.hz.size(); ++i) {
        const float hz = static_cast<float>(20.0 * std::pow(500.0, static_cast<double>(i) / 63.0));
        spreadModel_.hz[i] = hz;
        spreadModel_.w[i] = field_->widthCurve(hz) * m;
    }
    // Step 6: the law's own knees (updateLawCache, bass_stereo_field.h:648-651).
    spreadModel_.crossoverHz = a.crossoverHz;
    spreadModel_.lowerKneeHz = a.crossoverHz * std::exp2(-Field::kLowerKneeMaxOctaves * (1.0f - a.gravity));
    spreadModel_.upperKneeHz = a.crossoverHz * std::exp2(Field::kUpperKneeOctaves);
    spreadModel_.wide = a.mode == Field::ImageMode::Wide;
    spreadModel_.solid = solid;
    if (spreadView_ != nullptr) {
        spreadView_->invalid();  // plan 4.9: the view has no timer
    }
}

// ==============================================================================
// Phase 12 T025 - ADSRDisplay law adapter (FR-034, SC-024; plan 4.6.10, D-8)
// ==============================================================================
// The display emits cubic-normalised times (adsr_display.h:977, :1012, :1050 through timeMsToNormalized,
// :1413-1417), the raw sustain level (:1024) and (c + 1) * 0.5 curves (:1063). The registered amp times are
// logarithmic (morph_state.h:204-211), sustain and the curves Linear [0, 1] / [-1, 1] (:208-217). Both clamps agree:
// ADSRDisplay::kMinTimeMs (0.1) == kMinEnvelopeTimeMs, kDefaultMaxTimeMs (10000) == kMaxEnvelopeTimeMs.

static_assert(Krate::Plugins::ADSRDisplay::kMinTimeMs == Krate::DSP::kMinEnvelopeTimeMs &&
                  Krate::Plugins::ADSRDisplay::kDefaultMaxTimeMs == Krate::DSP::kMaxEnvelopeTimeMs,
              "plan 4.6.10: the ADSRDisplay and the envelope clamp to the same time range");

bool Controller::isAdsrParam(ParamID tag) noexcept {
    return tag >= kAmpAttackId && tag <= kAmpReleaseCurveId;
}

double Controller::adsrToRegistered(ParamID id, float n) noexcept {
    if (id != kAmpAttackId && id != kAmpDecayId && id != kAmpReleaseId) {
        return static_cast<double>(n);  // 422 and 424 - 426: the display already emits the registered law
    }
    const SoundParamRow* row = soundRowOf(id);
    if (row == nullptr) {
        return static_cast<double>(n);
    }
    // ADSRDisplay::normalizedToTimeMs (adsr_display.h:1420-1425) is private (:740), so its cubic is restated here, in
    // double: clamp(n, 0, 1)^3 * maxMs, clamped to [kMinTimeMs, maxMs], with maxMs = kMaxEnvelopeTimeMs (the max
    // wireAdsrDisplay sets on all three segments).
    const double c = std::clamp(static_cast<double>(n), 0.0, 1.0);
    const double maxMs = static_cast<double>(Krate::DSP::kMaxEnvelopeTimeMs);
    const double ms =
        std::clamp(c * c * c * maxMs, static_cast<double>(Krate::Plugins::ADSRDisplay::kMinTimeMs), maxMs);
    return normalizedFromPlain(lawOf(row->leaf), ms);
}

void Controller::wireAdsrDisplay(Krate::Plugins::ADSRDisplay* display) {
    adsrDisplay_ = display;
    if (display == nullptr) {
        return;
    }
    display->setAdsrBaseParamId(kAmpAttackId);          // 420 - 423
    display->setCurveBaseParamId(kAmpAttackCurveId);    // 424 - 426
    display->setAttackMaxMs(Krate::DSP::kMaxEnvelopeTimeMs);
    display->setDecayMaxMs(Krate::DSP::kMaxEnvelopeTimeMs);
    display->setReleaseMaxMs(Krate::DSP::kMaxEnvelopeTimeMs);
    display->setBezierEnabled(false);
    display->setParameterCallback([this](std::uint32_t id, float n) {
        if (!isAdsrParam(id)) {
            return;  // the display's other IDs (peak, Bezier) are unbound here
        }
        const double v = adsrToRegistered(id, n);
        static_cast<void>(setParamNormalized(id, v));
        static_cast<void>(performEdit(id, v));
    });
    display->setBeginEditCallback([this](std::uint32_t id) {
        if (isAdsrParam(id)) {
            static_cast<void>(beginEdit(id));
        }
    });
    display->setEndEditCallback([this](std::uint32_t id) {
        if (isAdsrParam(id)) {
            static_cast<void>(endEdit(id));
        }
    });
    refreshAdsrDisplay();
}

// The display ignores a setter while it drags (adsr_display.h:200-226), so the adapter's own setParamNormalized
// never fights the gesture.
void Controller::syncAdsrDisplay(ParamID tag) {
    if (adsrDisplay_ == nullptr) {
        return;
    }
    const SoundParamRow* row = soundRowOf(tag);
    if (row == nullptr) {
        return;
    }
    const float plain = static_cast<float>(plainFromNormalized(lawOf(row->leaf), getParamNormalized(tag)));
    switch (tag) {
        case kAmpAttackId: adsrDisplay_->setAttackMs(plain); break;
        case kAmpDecayId: adsrDisplay_->setDecayMs(plain); break;
        case kAmpSustainId: adsrDisplay_->setSustainLevel(plain); break;
        case kAmpReleaseId: adsrDisplay_->setReleaseMs(plain); break;
        case kAmpAttackCurveId: adsrDisplay_->setAttackCurve(plain); break;
        case kAmpDecayCurveId: adsrDisplay_->setDecayCurve(plain); break;
        case kAmpReleaseCurveId: adsrDisplay_->setReleaseCurve(plain); break;
        default: break;
    }
}

void Controller::refreshAdsrDisplay() {
    for (ParamID id = kAmpAttackId; id <= kAmpReleaseCurveId; ++id) {
        syncAdsrDisplay(id);
    }
}

// ==============================================================================
// Phase 12 T021 - DataExchange consumer (FR-071, FR-076, FR-091; plan 4.6.1; Vorago controller.cpp:303-329)
// ==============================================================================

void PLUGIN_API Controller::queueOpened(DataExchangeUserContextID userContextID, uint32 blockSize,
                                        TBool& dispatchOnBackgroundThread) {
    // UI-thread dispatch: cachedFrame_ is written and read on the UI thread only, so it needs no lock.
    dispatchOnBackgroundThread = static_cast<TBool>(false);
    if (userContextID != static_cast<DataExchangeUserContextID>(kProfundumFrameUserContextId)) {
        return;
    }
    // A (re)opened queue starts from silence: a frame of the previous activation (another rate) is never shown.
    cachedFrame_ = silenceFrame();
    ++queueOpenCount_;
    lastBlockSize_ = blockSize;
}

void PLUGIN_API Controller::queueClosed(DataExchangeUserContextID userContextID) {
    // Nothing to release: cachedFrame_ is a POD value.
    if (userContextID == static_cast<DataExchangeUserContextID>(kProfundumFrameUserContextId)) {
        ++queueCloseCount_;
    }
}

void PLUGIN_API Controller::onDataExchangeBlocksReceived(DataExchangeUserContextID userContextID, uint32 numBlocks,
                                                         DataExchangeBlock* blocks, TBool /*onBackgroundThread*/) {
    if (userContextID != static_cast<DataExchangeUserContextID>(kProfundumFrameUserContextId) || blocks == nullptr) {
        return;
    }
    // The newest valid block wins; older ones are stale. A block too short for the frame is ignored.
    for (uint32 i = 0; i < numBlocks; ++i) {
        if (blocks[i].data != nullptr && blocks[i].size >= sizeof(ProfundumSpectrumFrame)) {
            std::memcpy(&cachedFrame_, blocks[i].data, sizeof(ProfundumSpectrumFrame));
            ++framesReceived_;
        }
    }
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

// ==============================================================================
// Phase 12 T026 - gestures, VST3EditorDelegate, sync timer, session state (plan 4.6.2, 4.6.4, 4.6.7, 4.6.9)
// ==============================================================================

// Plan 4.6.4: the open-gesture count gates the sync tick (R-6); a macro gesture keeps its highlight on (SC-009).
tresult Controller::beginEdit(ParamID tag) {
    ++openGestures_;
    if (panel_ != nullptr && tag >= kMacroWeightId && tag <= kMacroCleanViolentId) {
        panel_->onMacroEdit(tag, true);
    }
    return EditControllerEx1::beginEdit(tag);
}

tresult Controller::performEdit(ParamID tag, ParamValue valueNormalized) {
    if (tag == paramBeingSet_) {
        return kResultOk;  // the VSTGUI echo of the performEdit that is setting this tag (controller.h)
    }
    return EditControllerEx1::performEdit(tag, valueNormalized);
}

tresult Controller::endEdit(ParamID tag) {
    if (openGestures_ > 0) {
        --openGestures_;
    }
    if (panel_ != nullptr && tag >= kMacroWeightId && tag <= kMacroCleanViolentId) {
        panel_->onMacroEdit(tag, false);
    }
    return EditControllerEx1::endEdit(tag);
}

// Plan 4.6.2. The view factory does not decorate a createCustomView view with the node's geometry, so the rect is
// built from the attributes (Vorago controller.cpp:352-357).
VSTGUI::CView* Controller::createCustomView(VSTGUI::UTF8StringPtr name, const VSTGUI::UIAttributes& attributes,
                                            const VSTGUI::IUIDescription* /*description*/,
                                            VSTGUI::VST3Editor* /*editor*/) {
    if (name == nullptr) {
        return nullptr;
    }
    VSTGUI::CPoint origin(0.0, 0.0);
    VSTGUI::CPoint size(100.0, 100.0);
    attributes.getPointAttribute("origin", origin);
    attributes.getPointAttribute("size", size);
    const VSTGUI::CRect viewRect(origin.x, origin.y, origin.x + size.x, origin.y + size.y);

    if (std::strcmp(name, "HarmonicBarView") == 0) {
        const std::string* variantName = attributes.getAttributeValue("bar-variant");
        const auto variant = (variantName != nullptr && *variantName == "core") ? UI::HarmonicBarView::Variant::Core
                                                                                 : UI::HarmonicBarView::Variant::Main;
        auto* view = new UI::HarmonicBarView(viewRect, &cachedFrame_, variant);
        for (UI::HarmonicBarView*& slot : barViews_) {
            if (slot == nullptr) {
                slot = view;
                break;
            }
        }
        return view;
    }
    if (std::strcmp(name, "SpreadCurveView") == 0) {
        spreadView_ = new UI::SpreadCurveView(viewRect, &spreadModel_);
        return spreadView_;
    }
    if (std::strcmp(name, "PresetBrowserButton") == 0) {
        // Phase 13 FR-059: the shared outline button; the panel sub-controller gives it its session tag and
        // listener in verifyView.
        return new Krate::Plugins::OutlineBrowserButton(viewRect, nullptr, -1, "PRESETS");
    }
    // The update banner, the manual check button and the version label (FR-059 amendment, VERSION_STR from
    // version.json): the shared views every Krate editor carries.
    if (auto* shared = Krate::Plugins::createSharedEditorView(name, attributes, updateChecker_.get(), VERSION_STR,
                                                              updateBannerView_)) {
        return shared;
    }
    return nullptr;
}

VSTGUI::IController* Controller::createSubController(VSTGUI::UTF8StringPtr name,
                                                     const VSTGUI::IUIDescription* /*description*/,
                                                     VSTGUI::VST3Editor* editor) {
    if (name == nullptr || std::strcmp(name, "ProfundumPanel") != 0) {
        return nullptr;
    }
    // Owned by the template root view (uidescription.cpp:741-755); it registers itself from its verifyView.
    return new UI::ProfundumPanelSubController(this, editor);
}

void Controller::didOpen(VSTGUI::VST3Editor* editor) {
    activeEditor_ = editor;
    // Phase 13 FR-030 (Vorago controller.cpp:383-392): the browser overlay lives in the frame, closed until the
    // header button opens it; its own save dialog is the save path.
    VSTGUI::CFrame* frame = (editor != nullptr) ? editor->getFrame() : nullptr;
    if (frame != nullptr && presetManager_ != nullptr && presetBrowserView_ == nullptr) {
        presetBrowserView_ =
            new Krate::Plugins::PresetBrowserView(frame->getViewSize(), presetManager_.get(), presetTabLabels_);
        frame->addView(presetBrowserView_);
    }
    if (editor != nullptr) {
        // Plan 4.6.2 step 2: the session zoom survives a reopen.
        editor->setAllowedZoomFactors(std::vector<double>(kZoomSteps.begin(), kZoomSteps.end()));
        editor->setZoomFactor(kZoomSteps[static_cast<std::size_t>(zoomIndex_)]);
    }
    // Plan 4.6.7 (D-10): the editor-side sync timer, open editor only (FR-090).
    stopEditorSyncTimer();
    editorSyncTimer_ = VSTGUI::owned(new VSTGUI::CVSTGUITimer(
        [this](VSTGUI::CVSTGUITimer* /*timer*/) { onEditorSyncTick(); }, kEditorSyncIntervalMs, true));
    // Plan 4.6.2 step 4.
    refreshMacroRings();
    refreshSpreadModel();
    refreshEndpointCaptions();
    if (panel_ != nullptr) {
        panel_->refreshAnchorDimming();
    }
    // Start the (24 h cooldown) update check and the banner's result polling.
    if (updateChecker_) {
        updateChecker_->checkForUpdate(false);
    }
    if (updateBannerView_ != nullptr) {
        updateBannerView_->startPolling();
    }
}

void Controller::willClose(VSTGUI::VST3Editor* /*editor*/) {
    // Phase 13 FR-030 (Vorago controller.cpp:411-418): close() unhooks the frame's keyboard hook while the frame is
    // still alive; the frame owns and destroys the view.
    if (presetBrowserView_ != nullptr) {
        if (presetBrowserView_->isOpen()) {
            presetBrowserView_->close();
        }
        presetBrowserView_ = nullptr;
    }
    if (updateBannerView_ != nullptr) {
        updateBannerView_->stopPolling();
        updateBannerView_ = nullptr;  // the frame owns and destroys it
    }
    stopEditorSyncTimer();
    // FR-091: every raw view pointer goes before the tree is destroyed.
    clearRingKnobs();
    wireAdsrDisplay(nullptr);
    barViews_.fill(nullptr);
    spreadView_ = nullptr;
    panel_ = nullptr;
    activeEditor_ = nullptr;
}

// The editor's own zoom menu offers the allowed factors (vst3editor.cpp:867-883); a pick there becomes the session
// step, and the zoom controls follow it.
void Controller::onZoomChanged(VSTGUI::VST3Editor* /*editor*/, double newZoom) {
    for (std::size_t i = 0; i < kZoomSteps.size(); ++i) {
        if (kZoomSteps[i] == newZoom) {
            zoomIndex_ = static_cast<int>(i);
            if (panel_ != nullptr) {
                panel_->showSessionState();
            }
            return;
        }
    }
}

void Controller::setActivePage(int page) noexcept {
    activePage_ = std::clamp(page, 0, kNumPages - 1);
}

void Controller::setActiveTab(int tab) noexcept {
    activeTab_ = std::clamp(tab, 0, kNumTabs - 1);
}

// Plan 4.6.9: session state only; no parameter, no state, no message.
void Controller::setZoomStep(int step) {
    zoomIndex_ = std::clamp(step, 0, static_cast<int>(kZoomSteps.size()) - 1);
    if (activeEditor_ != nullptr) {
        activeEditor_->setZoomFactor(kZoomSteps[static_cast<std::size_t>(zoomIndex_)]);
    }
}

// Phase 13 FR-030 / FR-059: the header button's action.
void Controller::openPresetBrowser() {
    if (presetBrowserView_ == nullptr || presetBrowserView_->isOpen()) {
        return;
    }
    presetBrowserView_->open();
}

void Controller::refreshEndpointCaptions() {
    if (panel_ != nullptr) {
        panel_->refreshCaptions();
    }
}

// Plan 4.6.7: one pull per tick. R-6: a tick during a gesture would re-display a mirror that may lag the newest
// performEdit by a block, so it is skipped. No editor -> no timer -> nothing is sent (FR-090).
void Controller::onEditorSyncTick() {
    if (editorSyncTimer_ == nullptr || openGestures_ > 0) {
        return;
    }
    static_cast<void>(sendMessageID(kMsgRequestSync));
}

void Controller::stopEditorSyncTimer() noexcept {
    if (editorSyncTimer_ != nullptr) {
        editorSyncTimer_->stop();
        editorSyncTimer_ = nullptr;
    }
}

bool Controller::viewPointersNullForTest() const noexcept {
    for (const auto& slots : ringKnobs_) {
        for (const OffsetRingKnob* knob : slots) {
            if (knob != nullptr) {
                return false;
            }
        }
    }
    return activeEditor_ == nullptr && barViews_[0] == nullptr && barViews_[1] == nullptr &&
           spreadView_ == nullptr && panel_ == nullptr && adsrDisplay_ == nullptr && presetBrowserView_ == nullptr &&
           updateBannerView_ == nullptr;
}

}  // namespace Profundum
