#pragma once

// ==============================================================================
// Profundum - Edit Controller (UI thread)   Plan section 2.6
// ==============================================================================
// Constitution Principle I: VST3 Architecture Separation. This header includes
// no processor header; processor and controller share only plugin_ids.h and the
// two parameters/*.h packs.
//
// FR-019: NO IMidiMapping, NO INoteExpressionController in Phase 10.
// FR-050: owns a PresetManager built from makeProfundumPresetConfig().
//
// The implementation is controller.cpp (plan section 2.6).
// ==============================================================================

#include "preset/preset_manager.h"

#include "public.sdk/source/vst/vsteditcontroller.h"

#include <memory>

namespace Profundum {

class Controller : public Steinberg::Vst::EditControllerEx1 {  // FR-019
public:
    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IEditController*>(new Controller());
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getParamStringByValue(Steinberg::Vst::ParamID tag,
                                                        Steinberg::Vst::ParamValue valueNormalized,
                                                        Steinberg::Vst::String128 string) override;
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) override;

    [[nodiscard]] Krate::Plugins::PresetManager* presetManagerForTest() const noexcept {
        return presetManager_.get();
    }

private:
    std::unique_ptr<Krate::Plugins::PresetManager> presetManager_;
};

}  // namespace Profundum
