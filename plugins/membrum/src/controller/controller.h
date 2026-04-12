#pragma once

// ==============================================================================
// Membrum Controller -- Phase 4 (per-pad parameters + proxy logic)
// ==============================================================================

#include "public.sdk/source/vst/vsteditcontroller.h"

namespace Steinberg { class IBStream; }

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

    // Phase 4: Override setParamNormalized to implement proxy logic
    Steinberg::tresult PLUGIN_API setParamNormalized(Steinberg::Vst::ParamID tag,
                                                      Steinberg::Vst::ParamValue value) override;

    // Phase 4: kit preset providers (FR-052)
    /// Produces a 9036-byte kit preset blob (v4 without selectedPadIndex)
    Steinberg::IBStream* kitPresetStateProvider();
    /// Loads a 9036-byte kit preset blob and syncs all controller params
    bool kitPresetLoadProvider(Steinberg::IBStream* stream);

private:
    // Phase 4: selected pad proxy logic helpers
    void syncGlobalProxyFromPad(int padIndex);
    void forwardGlobalToPad(Steinberg::Vst::ParamID globalId,
                            Steinberg::Vst::ParamValue value);

    int selectedPadIndex_ = 0;
    bool suppressProxyForward_ = false;  // prevent re-entrancy during pad switch
};

} // namespace Membrum
