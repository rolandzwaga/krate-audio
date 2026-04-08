// ==============================================================================
// Membrum Test Main
// ==============================================================================

#include <catch2/catch_session.hpp>

// Provide moduleHandle symbol required by VST3 SDK's moduleinit.cpp
void* moduleHandle = nullptr;

int main(int argc, char* argv[]) {
    return Catch::Session().run(argc, argv);
}
