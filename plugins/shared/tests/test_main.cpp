// ==============================================================================
// Shared Plugin Tests Main
// ==============================================================================
// Provides Catch2 main() function for the shared plugin tests executable.
// ==============================================================================

#include <catch2/catch_session.hpp>
#include <enable_ftz_daz.h>

// Provide moduleHandle symbol required by VST3 SDK's moduleinit.cpp
// In a real plugin this comes from dllmain.cpp, but tests don't have DLL entry points.
void* moduleHandle = nullptr;  // NOLINT(misc-use-internal-linkage,cppcoreguidelines-avoid-non-const-global-variables): moduleinit.cpp declares `extern void* moduleHandle;` and links against this exact definition

int main(int argc, char* argv[]) {
    enableFTZDAZ();
    return Catch::Session().run(argc, argv);
}
