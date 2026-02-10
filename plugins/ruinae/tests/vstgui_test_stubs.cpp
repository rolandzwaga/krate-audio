// ==============================================================================
// VSTGUI Test Stubs
// ==============================================================================
// Provides stub implementations of plugin entry point symbols required by
// vstgui_support (vstguieditor.cpp). Test executables don't have real plugin
// entry points, so these stubs satisfy the linker.
// ==============================================================================

#include "pluginterfaces/base/ipluginbase.h"

// vstgui_support references GetPluginFactory() which is normally provided by
// the plugin's entry.cpp. Test executables don't have one.
Steinberg::IPluginFactory* PLUGIN_API GetPluginFactory() { return nullptr; }

// On Linux, getPlatformModuleHandle() is not provided by the SDK's moduleinit.
// On Windows/macOS it is, so we only provide it on Linux to avoid duplicate symbols.
#ifdef __linux__
#include "public.sdk/source/main/moduleinit.h"
namespace Steinberg {
PlatformModuleHandle getPlatformModuleHandle() { return nullptr; }
} // namespace Steinberg
#endif
