// =============================================================================
// InnexusVoice Bow Exciter Field Tests (Spec 130, Phase 7 T059)
// =============================================================================
// Verifies that bowExciter is a proper field of InnexusVoice and that
// prepare()/reset() propagate to it.

#include "processor/innexus_voice.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("InnexusVoice has bowExciter field", "[innexus][voice][bow]")
{
    Innexus::InnexusVoice voice;

    // bowExciter should be a BowExciter instance
    // Verify it compiles and has the expected API
    REQUIRE_FALSE(voice.bowExciter.isPrepared());
    REQUIRE_FALSE(voice.bowExciter.isActive());
}

TEST_CASE("InnexusVoice prepare propagates to bowExciter", "[innexus][voice][bow]")
{
    Innexus::InnexusVoice voice;

    REQUIRE_FALSE(voice.bowExciter.isPrepared());

    voice.prepare(44100.0, 0);

    REQUIRE(voice.bowExciter.isPrepared());
}

TEST_CASE("InnexusVoice reset propagates to bowExciter", "[innexus][voice][bow]")
{
    Innexus::InnexusVoice voice;
    voice.prepare(44100.0, 0);

    // Trigger the bow so it becomes active
    voice.bowExciter.trigger(0.8f);
    REQUIRE(voice.bowExciter.isActive());

    // Reset should deactivate it
    voice.reset();
    REQUIRE_FALSE(voice.bowExciter.isActive());
}
