// ==============================================================================
// Profundum - Edit Controller implementation   Plan section 2.6
// ==============================================================================
// T010: initialize registers the eight Phase 10 parameters (global pack, then
// macro pack) and getParamStringByValue formats the continuous ones (FR-047).
// T012: setComponentState mirrors the processor stream (FR-045, FR-046).
// T019: the PresetManager (FR-050) and createView on the stock-view placeholder
// editor.uidesc (FR-054, FR-055). No UpdateChecker (FR-052).
// ==============================================================================

#include "controller/controller.h"

#include "parameters/global_params.h"
#include "parameters/macro_params.h"
#include "preset/profundum_preset_config.h"

#include "vstgui/plugin-bindings/vst3editor.h"

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

    registerGlobalParams(parameters);
    registerMacroParams(parameters);

    // FR-050. No UpdateChecker (FR-052).
    presetManager_ = std::make_unique<Krate::Plugins::PresetManager>(makeProfundumPresetConfig(),
                                                                     nullptr, this);
    return kResultOk;
}

tresult PLUGIN_API Controller::terminate() {
    presetManager_ = nullptr;
    return EditControllerEx1::terminate();
}

// FR-045 / FR-046 (plan 2.6): the processor's stream mirrored into the eight
// parameters. A future version is refused before any field is read; the packs
// sanitise each field before they normalise it and skip the call for an invalid one.
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
    const auto set = [this](ParamID id, double v) { setParamNormalized(id, v); };
    loadGlobalParamsToController(s, set);
    loadMacroParamsToController(s, set);
    return kResultOk;
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
    // Voice Mode and Polyphony are StringListParameters and format themselves.
    return EditControllerEx1::getParamStringByValue(tag, valueNormalized, string);
}

// FR-055 (model: plugins/vorago/src/controller/controller.cpp:271-278).
IPlugView* PLUGIN_API Controller::createView(FIDString name) {
    if (FIDStringsEqual(name, Vst::ViewType::kEditor)) {
        return new VSTGUI::VST3Editor(this, "editor", "editor.uidesc");
    }
    return nullptr;
}

}  // namespace Profundum
