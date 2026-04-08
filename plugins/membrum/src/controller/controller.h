#pragma once

// ==============================================================================
// Membrum Controller -- Stub for Phase 1 scaffolding
// ==============================================================================

#include "public.sdk/source/vst/vsteditcontroller.h"

namespace Membrum {

class Controller : public Steinberg::Vst::EditControllerEx1
{
public:
    Controller() = default;

    static Steinberg::FUnknown* createInstance(void*)
    {
        return static_cast<Steinberg::Vst::IEditController*>(new Controller());
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) override;
    Steinberg::IPlugView* PLUGIN_API createView(const char* name) override;
};

} // namespace Membrum
