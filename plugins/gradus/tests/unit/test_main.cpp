// ==============================================================================
// Gradus Test Main
// ==============================================================================

#include <catch2/catch_session.hpp>
#include <enable_ftz_daz.h>

// Provide moduleHandle symbol required by VST3 SDK's moduleinit.cpp
void* moduleHandle = nullptr;

int main(int argc, char* argv[]) {
    enableFTZDAZ();
    return Catch::Session().run(argc, argv);
}
