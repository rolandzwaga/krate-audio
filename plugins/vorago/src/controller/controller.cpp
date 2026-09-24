// ==============================================================================
// Vorago - Edit Controller implementation   Plan section 2.6
// ==============================================================================
// T017: registration (exactly 14 parameters), formatting and setComponentState.
// T018: createView on the stock-view placeholder editor.uidesc (FR-054, FR-055).
// ==============================================================================

#include "controller/controller.h"

#include "parameters/global_params.h"
#include "parameters/macro_params.h"
#include "plugin_ids.h"
#include "preset/vorago_preset_config.h"

#include "base/source/fstreamer.h"

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

    // Band order - the same order processParameterChanges routes and getState
    // writes. 2 + 12 = 14 (FR-041: no soft-limit, nothing else).
    registerGlobalParams(parameters);  // 0, 1
    registerMacroParams(parameters);   // 100-111

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

    // FR-047: the ...ToController helpers invert every processor mapping and are
    // EOF-safe, in getState's write order.
    auto setParam = [this](ParamID id, double v) { setParamNormalized(id, v); };
    loadGlobalParamsToController(streamer, setParam);
    loadMacroParamsToController(streamer, setParam);

    return kResultOk;
}

tresult PLUGIN_API Controller::getParamStringByValue(ParamID tag, ParamValue valueNormalized,
                                                     String128 string) {
    if (formatGlobalParam(tag, valueNormalized, string) == kResultOk) {
        return kResultOk;
    }
    if (formatMacroParam(tag, valueNormalized, string) == kResultOk) {
        return kResultOk;
    }
    // kPolyphonyId is a StringListParameter and formats itself.
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

}  // namespace Vorago
