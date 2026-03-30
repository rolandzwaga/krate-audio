// ==============================================================================
// Plugin Entry Point — Gradus (Standalone Arpeggiator)
// ==============================================================================

#include "plugin_ids.h"
#include "version.h"
#include "processor/processor.h"
#include "controller/controller.h"

#include "public.sdk/source/main/pluginfactory.h"

// Shared UI component registration — including these headers triggers
// the inline global ViewCreator instances that register custom classes
// (ArcKnob, ToggleButton, etc.) with VSTGUI's UIViewFactory.
#include "ui/action_button.h"
#include "ui/arc_knob.h"
#include "ui/toggle_button.h"

// ==============================================================================
// Plugin Factory Definition
// ==============================================================================

#define stringPluginName "Gradus"

BEGIN_FACTORY_DEF(
    stringCompanyName,
    stringVendorURL,
    stringVendorEmail
)

    // Processor Component Registration
    DEF_CLASS2(
        INLINE_UID_FROM_FUID(Gradus::kProcessorUID),
        PClassInfo::kManyInstances,
        kVstAudioEffectClass,
        stringPluginName,
        Steinberg::Vst::kDistributable,
        Gradus::kSubCategories,
        FULL_VERSION_STR,
        kVstVersionString,
        Gradus::Processor::createInstance
    )

    // Controller Component Registration
    DEF_CLASS2(
        INLINE_UID_FROM_FUID(Gradus::kControllerUID),
        PClassInfo::kManyInstances,
        kVstComponentControllerClass,
        stringPluginName "Controller",
        0,
        "",
        FULL_VERSION_STR,
        kVstVersionString,
        Gradus::Controller::createInstance
    )

END_FACTORY
