// ==============================================================================
// Innexus Test Main
// ==============================================================================
// Provides Catch2 main() function for the Innexus test executable.
// ==============================================================================

#include <catch2/catch_session.hpp>
#include <enable_ftz_daz.h>

// Provide moduleHandle symbol required by VST3 SDK's moduleinit.cpp
// In a real plugin this comes from dllmain.cpp, but tests don't have DLL entry points.
void* moduleHandle = nullptr;

int main(int argc, char* argv[]) {
    enableFTZDAZ();
    return Catch::Session().run(argc, argv);
}
