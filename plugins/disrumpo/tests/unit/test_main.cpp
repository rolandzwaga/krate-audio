// ==============================================================================
// Disrumpo Test Main
// ==============================================================================
// Provides Catch2 main() function for the Disrumpo test executable.
// ==============================================================================

#include <catch2/catch_session.hpp>

int main(int argc, char* argv[]) {
    return Catch::Session().run(argc, argv);
}
