// ==============================================================================
// VSTGUI Test Stubs
// ==============================================================================
// Provides the GetPluginFactory symbol and the moduleHandle global that the
// VST3 SDK module-init objects (linked in for vstgui_support) reference, so the
// test executable links without pulling in the plugin entry point.

#include "pluginterfaces/base/ipluginbase.h"

// Referenced by the VST3 SDK module init translation unit.
void* moduleHandle = nullptr;

extern "C" {

Steinberg::IPluginFactory* PLUGIN_API GetPluginFactory()
{
    return nullptr;
}

} // extern "C"
