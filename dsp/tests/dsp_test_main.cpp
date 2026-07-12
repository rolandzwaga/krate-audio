// ==============================================================================
// Shared Catch2 entry point for all KrateDSP test executables.
// ==============================================================================
// The dsp_tests suite was split into one executable per layer (dsp_core_tests,
// dsp_primitives_tests, ...). Each needs a main, and each must enable FTZ/DAZ so
// denormal handling matches the audio-thread runtime before any test runs.
// ==============================================================================

#include <catch2/catch_session.hpp>
#include <enable_ftz_daz.h>

int main(int argc, char* argv[]) {
    enableFTZDAZ();
    return Catch::Session().run(argc, argv);
}
