#pragma once

// ==============================================================================
// Vorago - Edit Controller (UI thread)   Plan section 2.6
// ==============================================================================
// Constitution Principle I: VST3 Architecture Separation. This header includes
// NO processor header.
//
// IMidiMapping (Phase 12 T034, FR-031): on bus 0, any channel, CC64
// (kCtrlSustainOnOff) maps to kSustainPedalId and channel aftertouch
// (kAfterTouch) maps to kChannelPressureId; every other controller is
// unmapped. NO INoteExpressionController (FR-019, OQ-7 ruling). No
// createCustomView / verifyView: stock views only in Phase 11 (FR-055).
//
// The class declaration is FINAL (T005). The bodies in controller.cpp start as
// stubs and are replaced by T017 and T018.
// ==============================================================================

#include "preset/preset_manager.h"

#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "vstgui/plugin-bindings/vst3editor.h"

#include <memory>

namespace Vorago {

class Controller : public Steinberg::Vst::EditControllerEx1,
                   public Steinberg::Vst::IMidiMapping,
                   public VSTGUI::VST3EditorDelegate {
public:
    Controller() = default;
    ~Controller() override = default;

    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IEditController*>(new Controller());
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getParamStringByValue(
        Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue valueNormalized,
        Steinberg::Vst::String128 string) override;
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) override;

    // IMidiMapping (FR-031)
    Steinberg::tresult PLUGIN_API getMidiControllerAssignment(
        Steinberg::int32 busIndex, Steinberg::int16 channel,
        Steinberg::Vst::CtrlNumber midiControllerNumber, Steinberg::Vst::ParamID& id) override;

    DEFINE_INTERFACES
        DEF_INTERFACE(Steinberg::Vst::IMidiMapping)
    END_DEFINE_INTERFACES(EditControllerEx1)

    DELEGATE_REFCOUNT(EditControllerEx1)

    [[nodiscard]] Krate::Plugins::PresetManager* presetManagerForTest() const noexcept {
        return presetManager_.get();  // model plugins/seraphis/src/controller/controller.h:196
    }

private:
    std::unique_ptr<Krate::Plugins::PresetManager> presetManager_;
};

}  // namespace Vorago
