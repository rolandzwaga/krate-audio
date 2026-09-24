// ==============================================================================
// Plugin Entry Point
// ==============================================================================
// This file contains the plugin factory that the host uses to create
// processor and controller instances.
//
// Constitution Principle I: VST3 Architecture Separation
// - Processor and Controller are registered as separate classes
// - Each has its own unique FUID
// - Host can instantiate them independently
//
// FR-018: this file MUST NOT include any ui/*.h or custom-view header. Phase 11
// registers no custom views (the placeholder editor uses stock VSTGUI views
// only); Phase 13 adds the view-creator includes here when the real interface
// lands.
// ==============================================================================

#include "plugin_ids.h"
#include "version.h"
#include "processor/processor.h"
#include "controller/controller.h"

#include "public.sdk/source/main/pluginfactory.h"

// ==============================================================================
// Plugin Factory Definition
// ==============================================================================
// An IDENTICAL redefinition of the macro cmake/version.h.in generates from
// version.json's "name" -- so it is warning-free. Keep the spellings identical.

#define stringPluginName "Vorago"

BEGIN_FACTORY_DEF(
    stringCompanyName,      // Vendor name
    stringVendorURL,        // Vendor URL (defined in version.h)
    stringVendorEmail       // Vendor email (defined in version.h)
)

    // ==========================================================================
    // Processor Component Registration
    // ==========================================================================
    DEF_CLASS2(
        INLINE_UID_FROM_FUID(Vorago::kProcessorUID),
        PClassInfo::kManyInstances,           // cardinality
        kVstAudioEffectClass,                 // component category
        stringPluginName,                     // plugin name
        Steinberg::Vst::kDistributable,       // Constitution: enable separation
        Vorago::kSubCategories,               // subcategories
        FULL_VERSION_STR,                     // version
        kVstVersionString,                    // SDK version
        Vorago::Processor::createInstance     // factory function
    )

    // ==========================================================================
    // Controller Component Registration
    // ==========================================================================
    DEF_CLASS2(
        INLINE_UID_FROM_FUID(Vorago::kControllerUID),
        PClassInfo::kManyInstances,           // cardinality
        kVstComponentControllerClass,         // component category
        stringPluginName "Controller",        // controller name
        0,                                    // unused for controller
        "",                                   // unused for controller
        FULL_VERSION_STR,                     // version
        kVstVersionString,                    // SDK version
        Vorago::Controller::createInstance    // factory function
    )

END_FACTORY
