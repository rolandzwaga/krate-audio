#include "controller/controller.h"
#include "plugin_ids.h"
#include "dsp/pad_config.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

using namespace Membrum;
using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Matchers::WithinAbs;

namespace {

/// Mirror of ADSRDisplay::timeMsToNormalized: cubic encoding of ms -> norm.
float cubicEncodeMsToNorm(float ms, float maxMs) noexcept
{
    return std::cbrt(std::clamp(ms, 0.001f, maxMs) / maxMs);
}

/// Mirror of the controller's cubic decode (and the processor's cubic decode):
/// norm^3 * maxMs -> ms.
float cubicDecodeNormToMs(float norm, float maxMs) noexcept
{
    const float n = std::clamp(norm, 0.0f, 1.0f);
    return n * n * n * maxMs;
}

} // namespace

TEST_CASE("ADSR edit persists across pad reselect", "[adsr_persist]")
{
    Controller c;
    REQUIRE(c.initialize(nullptr) == kResultOk);

    // Initial state: pad 0 selected, default Attack.
    const ParamID attackGlobal = static_cast<ParamID>(kToneShaperFilterEnvAttackId);
    const ParamID attackPad0   = static_cast<ParamID>(padParamId(0, kPadTSFilterEnvAttack));

    // User drags ADSR display: edit goes to global.
    constexpr double newAttack = 0.42;
    c.setParamNormalized(attackGlobal, newAttack);

    // Forward should have copied global -> per-pad.
    REQUIRE(c.getParamNormalized(attackGlobal) == newAttack);
    REQUIRE(c.getParamNormalized(attackPad0) == newAttack);

    // User clicks pad 0 (same pad): selectCallback fires.
    c.setParamNormalized(static_cast<ParamID>(kSelectedPadId), 0.0);

    // After sync, global should still reflect user's edit (per-pad still has it).
    REQUIRE(c.getParamNormalized(attackGlobal) == newAttack);
    REQUIRE(c.getParamNormalized(attackPad0)   == newAttack);

    c.terminate();
}

// ==============================================================================
// Cubic round-trip: the value the user drags to (in ms) must come back as the
// same ms after a sync push. The ADSRDisplay drag path encodes ms cubically
// (norm = cbrt(ms/max)); the controller and processor must decode cubically
// to preserve the round trip. Before the cubic fix, this would round-trip a
// dragged 100ms to ~292ms on the next pad-switch sync.
// ==============================================================================
TEST_CASE("Filter env display round-trips dragged ms after pad reselect",
          "[adsr_persist]")
{
    Controller c;
    REQUIRE(c.initialize(nullptr) == kResultOk);

    struct Case
    {
        ParamID globalId;
        float   maxMs;
        float   targetMs;
    };

    const std::vector<Case> cases = {
        { static_cast<ParamID>(kToneShaperFilterEnvAttackId),  500.0f,  100.0f },
        { static_cast<ParamID>(kToneShaperFilterEnvAttackId),  500.0f,  250.0f },
        { static_cast<ParamID>(kToneShaperFilterEnvDecayId),  2000.0f,  300.0f },
        { static_cast<ParamID>(kToneShaperFilterEnvDecayId),  2000.0f, 1500.0f },
        { static_cast<ParamID>(kToneShaperFilterEnvReleaseId),2000.0f,  500.0f },
    };

    for (const auto& tc : cases)
    {
        // Simulate the ADSRDisplay's drag callback: it encodes target-ms as
        // norm = cbrt(target/max) and sends that to setParamNormalized.
        const float dragNorm = cubicEncodeMsToNorm(tc.targetMs, tc.maxMs);
        c.setParamNormalized(tc.globalId, dragNorm);

        // Click pad 0 -- triggers syncGlobalProxyFromPad and the display
        // refresh that the user reports as a "reset".
        c.setParamNormalized(static_cast<ParamID>(kSelectedPadId), 0.0);

        // Reconstruct what updateFilterEnvDisplay would push to the display:
        // cubic decode of the synced normalized value.
        const float syncedNorm = static_cast<float>(
            c.getParamNormalized(tc.globalId));
        const float displayedMs = cubicDecodeNormToMs(syncedNorm, tc.maxMs);

        INFO("target=" << tc.targetMs << " dragNorm=" << dragNorm
             << " syncedNorm=" << syncedNorm << " displayedMs=" << displayedMs);
        // Allow a tiny epsilon for the cbrt -> cube round trip.
        REQUIRE_THAT(displayedMs, WithinAbs(tc.targetMs, 0.5));
    }

    c.terminate();
}
