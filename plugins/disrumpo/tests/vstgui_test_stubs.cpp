// ==============================================================================
// VSTGUI Test Stubs
// ==============================================================================
// Provides symbols required by vstgui_support that normally come from the
// plugin entry point (entry.cpp) and platform main (linuxmain.cpp etc.).
// Without these, Linux linker fails with undefined references to
// GetPluginFactory, getPlatformModuleHandle, ModuleInitializer, etc.
//
// Note: moduleHandle is provided by test_main.cpp (not duplicated here).
// ==============================================================================

#include "pluginterfaces/base/ipluginbase.h"

extern "C" {

Steinberg::IPluginFactory* PLUGIN_API GetPluginFactory()
{
    return nullptr;
}

} // extern "C"
