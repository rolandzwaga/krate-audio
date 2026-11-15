// ==============================================================================
// VST3 plugin-entry stubs for the standalone testbench
// ==============================================================================
// ArcKnob dynamic_casts to Steinberg::Vst::VSTGUIEditor, whose typeinfo lives in
// vstgui_support's vstguieditor.cpp on the Itanium ABI (Linux/macOS). That TU
// references the plugin-entry symbols below. The testbench is an app, not a plugin:
// nothing calls InitModule, so vstguieditor.cpp's module callbacks never run and
// these definitions only satisfy the linker. Same pattern as shared_tests
// (plugins/shared/tests/vstgui_test_stubs.cpp, test_main.cpp).
// ==============================================================================

#include "pluginterfaces/base/ipluginbase.h"

// moduleinit.cpp and vstguieditor.cpp declare `extern void* moduleHandle;`.
void* moduleHandle = nullptr; // NOLINT(misc-use-internal-linkage,cppcoreguidelines-avoid-non-const-global-variables)

extern "C" {

Steinberg::IPluginFactory* PLUGIN_API GetPluginFactory()
{
    return nullptr;
}

} // extern "C"
