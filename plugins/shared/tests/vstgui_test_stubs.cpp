// ==============================================================================
// VSTGUI Test Stubs
// ==============================================================================
// Note: moduleHandle is provided by test_main.cpp (not duplicated here).
// ==============================================================================

#include "pluginterfaces/base/ipluginbase.h"

extern "C" {

Steinberg::IPluginFactory* PLUGIN_API GetPluginFactory()
{
    return nullptr;
}

} // extern "C"
