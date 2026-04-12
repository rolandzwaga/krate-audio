// ==============================================================================
// Phase 3.0 scaffolding test -- sizeof(DrumVoice) decision
// ==============================================================================
// plan.md Open Question #1 / Complexity Tracking:
//
//   Decision criteria: if `32 * sizeof(DrumVoice) > 1 MiB`
//   (i.e. sizeof(DrumVoice) > 32768), use per-slot single-fade reservation
//   (one `releasingVoice` slot per main slot, one fade-out in-flight maximum
//   per slot); otherwise, keep the two-array approach with `voices_[16]` +
//   `releasingVoices_[16]`.
//
// This test merely captures the measured size so the decision is documented
// in the Phase 3 compliance table. The value is also required by plan.md's
// Complexity Tracking row "sizeof(DrumVoice)".
// ==============================================================================

#include "../../../src/dsp/drum_voice.h"
#include "../../../src/voice_pool/voice_pool.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>

using Membrum::DrumVoice;
using Membrum::kMaxVoices;

TEST_CASE("DrumVoice sizeof decision", "[scaffolding][voice_pool]")
{
    const std::size_t perVoice    = sizeof(DrumVoice);
    const std::size_t twoArrayTot = 2U * static_cast<std::size_t>(kMaxVoices) * perVoice;
    const std::size_t oneMiB      = 1024U * 1024U;

    CAPTURE(perVoice);
    CAPTURE(twoArrayTot);
    CAPTURE(oneMiB);

    REQUIRE(perVoice > 0U);

    // Inform the human reader which approach plan.md's decision criteria
    // picks. This is purely informational -- the test passes either way.
    const bool twoArrayApproachFits = (perVoice <= 32768U);
    CAPTURE(twoArrayApproachFits);
}
