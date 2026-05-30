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
#include "ui/icon_segment_button.h"
#include "ui/toggle_button.h"

#include "plugin_factory.h"

// ==============================================================================
// Plugin Factory Definition
// ==============================================================================

#define stringPluginName "Iterum"

KRATE_DEFINE_PLUGIN_FACTORY(Iterum, stringPluginName, Iterum::kSubCategories)

// ==============================================================================
// Module Entry/Exit (Platform Specific)
// ==============================================================================
// The VST3 SDK handles module initialization via macros in pluginfactory.h
// No additional code needed here for most cases.
// ==============================================================================
