// Shard-balance slot 3 of the Vorago lever bounds sentinels (vorago_lever_bounded_arms.h).

#include <catch2/catch_test_macros.hpp>

#include "vorago_lever_bounded_arms.h"

TEST_CASE("VoragoEngine_EcosystemLeverBounded_NaturalColony_96k", "[systems][vorago]") {
    VoragoLeverBounded::runEcosystemLeverBounded(VoragoLeverBounded::EcoArm::Natural, 96000.0);
}

TEST_CASE("VoragoEngine_CapabilityLeverBounded_Gravity1_48k", "[systems][vorago]") {
    VoragoLeverBounded::runCapabilityLeverBounded(1.0f, 48000.0);
}
