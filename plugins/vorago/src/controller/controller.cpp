// ==============================================================================
// Vorago - Edit Controller implementation   Plan section 2.6
// ==============================================================================
// T017: registration, formatting and setComponentState (Phase 11: 14 parameters).
// Phase 12 T033: registration, description and formatting of all 108 parameters
// (FR-041, SC-018) - global, macros, then the 14 packs in band order.
// T018: createView on the stock-view placeholder editor.uidesc (FR-054, FR-055).
// Phase 12 T034: IMidiMapping - CC64 and channel aftertouch (FR-031, SC-013 (5)).
// Phase 12 T042: setComponentState mirrors the v2 state (FR-040, plan 4.9); a v1
// stream returns every Phase 12 field to its registered default (C-7).
// ==============================================================================

#include "controller/controller.h"

#include "parameters/bloom_params.h"
#include "parameters/body_params.h"
#include "parameters/cloud_params.h"
#include "parameters/ecology_params.h"
#include "parameters/ecosystem_params.h"
#include "parameters/envelope_params.h"
#include "parameters/events_params.h"
#include "parameters/ghost_params.h"
#include "parameters/global_params.h"
#include "parameters/life_params.h"
#include "parameters/macro_params.h"
#include "parameters/noise_params.h"
#include "parameters/resonance_params.h"
#include "parameters/smear_params.h"
#include "parameters/space_params.h"
#include "parameters/sub_params.h"
#include "plugin_ids.h"
#include "preset/vorago_preset_config.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"

#include <array>
#include <memory>
#include <type_traits>

// Compile touch point for the update config (FR-052, plan section 2.6): the
// header is included by nothing else, and a .h in a CMake source list is
// HEADER_FILE_ONLY, so this static_assert is the ONLY thing that compiles it.
// No UpdateChecker is ever instantiated (FR-052).
#include "update/vorago_update_config.h"
static_assert(std::is_same_v<decltype(Vorago::makeVoragoUpdateConfig()),
                             Krate::Plugins::UpdateCheckerConfig>);

namespace Vorago {

using namespace Steinberg;
using namespace Steinberg::Vst;

tresult PLUGIN_API Controller::initialize(FUnknown* context) {
    const tresult result = EditControllerEx1::initialize(context);
    if (result != kResultOk) {
        return result;
    }

    // Band order == ascending ID (plan section 3.2): 6 + 12 + 90 = 108 (FR-041:
    // no soft-limit, nothing else).
    registerGlobalParams(parameters);     // 0-5
    registerMacroParams(parameters);      // 100-111
    registerCloudParams(parameters);      // 200-206
    registerNoiseParams(parameters);      // 300-353
    registerResonanceParams(parameters);  // 400-403
    registerEcologyParams(parameters);    // 500-515
    registerSubParams(parameters);        // 600-612
    registerSmearParams(parameters);      // 700-702
    registerEventsParams(parameters);     // 800
    registerEcosystemParams(parameters);  // 900
    registerBodyParams(parameters);       // 1000-1005
    registerSpaceParams(parameters);      // 1100-1115
    registerEnvelopeParams(parameters);   // 1200-1206
    registerBloomParams(parameters);      // 1300-1301
    registerGhostParams(parameters);      // 1400-1403
    registerLifeParams(parameters);       // 1500-1502

    // FR-050. No state/load providers until the Phase 13 browser exists; no
    // UpdateChecker (FR-052).
    presetManager_ = std::make_unique<Krate::Plugins::PresetManager>(makeVoragoPresetConfig(),
                                                                     nullptr, this);
    return kResultOk;
}

tresult PLUGIN_API Controller::terminate() {
    presetManager_ = nullptr;
    return EditControllerEx1::terminate();
}

tresult PLUGIN_API Controller::setComponentState(IBStream* state) {
    if (state == nullptr) {
        return kResultFalse;
    }

    IBStreamer streamer(state, kLittleEndian);

    int32 version = 0;
    if (!streamer.readInt32(version)) {
        return kResultFalse;
    }
    if (version > kCurrentStateVersion) {
        return kResultFalse;
    }

    // FR-047 / FR-040: the ...ToController helpers invert every processor mapping
    // and are EOF-safe, in getState's write order (plan 4.9). After a failed read
    // every later read fails too (the stream is at its end), matching the
    // processor's short-circuit chain.
    auto setParam = [this](ParamID id, double v) { setParamNormalized(id, v); };
    const auto loadV2Tail = [&setParam](IBStreamer& in) {
        loadGlobalParamsV2ExtToController(in, setParam);
        loadCloudParamsToController(in, setParam);
        loadNoiseParamsToController(in, setParam);
        loadResonanceParamsToController(in, setParam);
        loadEcologyParamsToController(in, setParam);
        loadSubParamsToController(in, setParam);
        loadSmearParamsToController(in, setParam);
        loadEventsParamsToController(in, setParam);
        loadEcosystemParamsToController(in, setParam);
        loadBodyParamsToController(in, setParam);
        loadSpaceParamsToController(in, setParam);
        loadEnvelopeParamsToController(in, setParam);
        loadBloomParamsToController(in, setParam);
        loadGhostParamsToController(in, setParam);
        loadLifeParamsToController(in, setParam);
    };
    loadGlobalParamsToController(streamer, setParam);
    loadMacroParamsToController(streamer, setParam);
    if (version >= 2) {
        loadV2Tail(streamer);
    } else {
        // C-7 / FR-040 mirror of Processor::setState: a version < 2 stream leaves
        // every Phase 12 field at its registered default. The default-constructed
        // packs' v2 tail is serialized and fed through the same inverse mappings.
        auto tail = owned(new MemoryStream());
        IBStreamer out(tail, kLittleEndian);
        saveGlobalParamsV2Ext(GlobalParams{}, out);
        saveCloudParams(CloudParams{}, out);
        saveNoiseParams(NoiseParams{}, out);
        saveResonanceParams(ResonanceParams{}, out);
        saveEcologyParams(EcologyParams{}, out);
        saveSubParams(SubParams{}, out);
        saveSmearParams(SmearParams{}, out);
        saveEventsParams(EventsParams{}, out);
        saveEcosystemParams(EcosystemParams{}, out);
        saveBodyParams(BodyParams{}, out);
        saveSpaceParams(SpaceParams{}, out);
        saveEnvelopeParams(EnvelopeParams{}, out);
        saveBloomParams(BloomParams{}, out);
        saveGhostParams(GhostParams{}, out);
        saveLifeParams(LifeParams{}, out);
        tail->seek(0, IBStream::kIBSeekSet, nullptr);
        loadV2Tail(out);
    }

    return kResultOk;
}

tresult PLUGIN_API Controller::getParamStringByValue(ParamID tag, ParamValue valueNormalized,
                                                     String128 string) {
    // Each pack formats only its own continuous IDs (kResultFalse otherwise).
    using Formatter = tresult (*)(ParamID, ParamValue, String128);
    static constexpr std::array<Formatter, 16> kFormatters = {
        formatGlobalParam,    formatMacroParam,     formatCloudParam,    formatNoiseParam,
        formatResonanceParam, formatEcologyParam,   formatSubParam,      formatSmearParam,
        formatEventsParam,    formatEcosystemParam, formatBodyParam,     formatSpaceParam,
        formatEnvelopeParam,  formatBloomParam,     formatGhostParam,    formatLifeParam};
    for (const Formatter format : kFormatters) {
        if (format(tag, valueNormalized, string) == kResultOk) {
            return kResultOk;
        }
    }
    // The StringListParameters (polyphony, seed, noise model/type, anchor, loop
    // filters, materials, freeze, envelope mode, ghost triggers) format themselves.
    return EditControllerEx1::getParamStringByValue(tag, valueNormalized, string);
}

IPlugView* PLUGIN_API Controller::createView(FIDString name) {
    // Model plugins/seraphis/src/controller/controller.cpp:434-436. No
    // createCustomView / verifyView and no raw view pointers held (FR-055).
    if (FIDStringsEqual(name, Vst::ViewType::kEditor)) {
        return new VSTGUI::VST3Editor(this, "editor", "editor.uidesc");
    }
    return nullptr;
}

tresult PLUGIN_API Controller::getMidiControllerAssignment(int32 busIndex, int16 /*channel*/,
                                                           CtrlNumber midiControllerNumber,
                                                           ParamID& id) {
    // Model plugins/disrumpo/src/controller/controller.cpp:711-726. Bus 0 only;
    // every channel maps to the same two performance parameters (FR-031).
    if (busIndex != 0) {
        return kResultFalse;
    }
    if (midiControllerNumber == kCtrlSustainOnOff) {
        id = kSustainPedalId;
        return kResultOk;
    }
    if (midiControllerNumber == kAfterTouch) {
        id = kChannelPressureId;
        return kResultOk;
    }
    return kResultFalse;
}

}  // namespace Vorago
