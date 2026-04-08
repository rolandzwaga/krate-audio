// ==============================================================================
// Plugin Entry Point -- Membrum (Synthesized Drum Machine)
// ==============================================================================

#include "plugin_ids.h"
#include "version.h"
#include "processor/processor.h"
#include "controller/controller.h"

#include "public.sdk/source/main/pluginfactory.h"

// ==============================================================================
// Plugin Factory Definition
// ==============================================================================

BEGIN_FACTORY_DEF(
    stringCompanyName,
    stringVendorURL,
    stringVendorEmail
)

    // Processor Component Registration
    DEF_CLASS2(
        INLINE_UID_FROM_FUID(Membrum::kProcessorUID),
        PClassInfo::kManyInstances,
        kVstAudioEffectClass,
        stringPluginName,
        Steinberg::Vst::kDistributable,
        Membrum::kSubCategories,
        FULL_VERSION_STR,
        kVstVersionString,
        Membrum::Processor::createInstance
    )

    // Controller Component Registration
    DEF_CLASS2(
        INLINE_UID_FROM_FUID(Membrum::kControllerUID),
        PClassInfo::kManyInstances,
        kVstComponentControllerClass,
        stringPluginName "Controller",
        0,
        "",
        FULL_VERSION_STR,
        kVstVersionString,
        Membrum::Controller::createInstance
    )

END_FACTORY
