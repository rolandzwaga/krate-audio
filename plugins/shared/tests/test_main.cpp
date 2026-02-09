// ==============================================================================
// Shared Plugin Tests Main
// ==============================================================================
// Provides Catch2 main() function for the shared plugin tests executable.
// ==============================================================================

#include <catch2/catch_session.hpp>

// Provide moduleHandle symbol required by VST3 SDK's moduleinit.cpp
// In a real plugin this comes from dllmain.cpp, but tests don't have DLL entry points.
void* moduleHandle = nullptr;

int main(int argc, char* argv[]) {
    return Catch::Session().run(argc, argv);
}
