// Shard-balance slot 4 of the Vorago lever bounds sentinels (vorago_lever_bounded_arms.h).

#include <catch2/catch_test_macros.hpp>

#include "vorago_lever_bounded_arms.h"

TEST_CASE("VoragoEngine_CapabilityLeverBounded_Gravity0_96k", "[systems][vorago]") {
    VoragoLeverBounded::runCapabilityLeverBounded(0.0f, 96000.0);
}

TEST_CASE("VoragoEngine_EcosystemLeverBounded_InjectedL1_44k1", "[systems][vorago]") {
    VoragoLeverBounded::runEcosystemLeverBounded(VoragoLeverBounded::EcoArm::Injected, 44100.0);
}
