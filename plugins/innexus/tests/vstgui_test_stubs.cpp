// ==============================================================================
// VSTGUI Test Stubs
// ==============================================================================
// Provides the GetPluginFactory() symbol that the VST3 SDK expects.
// In the real plugin this comes from entry.cpp; in tests we provide a stub.
// ==============================================================================

#include "pluginterfaces/base/ipluginbase.h"

extern "C" {

Steinberg::IPluginFactory* PLUGIN_API GetPluginFactory()
{
    return nullptr;
}

} // extern "C"
