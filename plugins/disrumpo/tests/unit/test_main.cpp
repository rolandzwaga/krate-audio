// ==============================================================================
// Disrumpo Test Main
// ==============================================================================
// Provides Catch2 main() function for the Disrumpo test executable.
// ==============================================================================

#include <catch2/catch_session.hpp>

// Provide moduleHandle symbol required by VST3 SDK's moduleinit.cpp
// (needed when linking controller.cpp or any full VST3 component)
void* moduleHandle = nullptr;

int main(int argc, char* argv[]) {
    return Catch::Session().run(argc, argv);
}
