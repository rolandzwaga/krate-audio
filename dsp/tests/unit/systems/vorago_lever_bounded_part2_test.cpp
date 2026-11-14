// Shard-balance slot 2 of the Vorago lever bounds sentinels (vorago_lever_bounded_arms.h).

#include <catch2/catch_test_macros.hpp>

#include "vorago_lever_bounded_arms.h"

TEST_CASE("VoragoEngine_EcosystemLeverBounded_NaturalColony_48k", "[systems][vorago]") {
    VoragoLeverBounded::runEcosystemLeverBounded(VoragoLeverBounded::EcoArm::Natural, 48000.0);
}

TEST_CASE("VoragoEngine_CapabilityLeverBounded_Gravity1_44k1", "[systems][vorago]") {
    VoragoLeverBounded::runCapabilityLeverBounded(1.0f, 44100.0);
}
