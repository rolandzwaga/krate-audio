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
// ==============================================================================

#include "plugin_ids.h"
#include "version.h"
#include "processor/processor.h"
#include "controller/controller.h"

// Shared UI controls - include triggers static ViewCreator registration
#include "ui/arc_knob.h"
#include "ui/fieldset_container.h"
#include "ui/step_pattern_editor.h"
#include "ui/xy_morph_pad.h"
#include "ui/adsr_display.h"
#include "ui/oscillator_type_selector.h"

#include "public.sdk/source/main/pluginfactory.h"

// ==============================================================================
// Plugin Factory Definition
// ==============================================================================

#define stringPluginName "Ruinae"

BEGIN_FACTORY_DEF(
    stringCompanyName,      // Vendor name
    stringVendorURL,        // Vendor URL (defined in version.h as empty)
    stringVendorEmail       // Vendor email (defined in version.h as empty)
)

    // ==========================================================================
    // Processor Component Registration
    // ==========================================================================
    DEF_CLASS2(
        INLINE_UID_FROM_FUID(Ruinae::kProcessorUID),
        PClassInfo::kManyInstances,           // cardinality
        kVstAudioEffectClass,                 // component category
        stringPluginName,                     // plugin name
        Steinberg::Vst::kDistributable,       // Constitution: enable separation
        Ruinae::kSubCategories,              // subcategories
        FULL_VERSION_STR,                     // version
        kVstVersionString,                    // SDK version
        Ruinae::Processor::createInstance    // factory function
    )

    // ==========================================================================
    // Controller Component Registration
    // ==========================================================================
    DEF_CLASS2(
        INLINE_UID_FROM_FUID(Ruinae::kControllerUID),
        PClassInfo::kManyInstances,           // cardinality
        kVstComponentControllerClass,         // component category
        stringPluginName "Controller",        // controller name
        0,                                    // unused for controller
        "",                                   // unused for controller
        FULL_VERSION_STR,                     // version
        kVstVersionString,                    // SDK version
        Ruinae::Controller::createInstance   // factory function
    )

END_FACTORY

// ==============================================================================
// Module Entry/Exit (Platform Specific)
// ==============================================================================
// The VST3 SDK handles module initialization via macros in pluginfactory.h
// No additional code needed here for most cases.
// ==============================================================================
