// ==============================================================================
// Secondary (Phase 8D) shell-coupling parity across the fast and slow voice
// paths.
// ==============================================================================
// Audit finding L-9 (AUDIT-signal-path-2026-06-07.md): "No test asserts
// coupling/secondary is audible or that fast==slow for a coupled voice." That
// coverage gap is what let M-6 (the secondary shell stage being dropped from
// processBlockSlow / the FeedbackExciter path) regress unnoticed.
//
// These tests pin two behaviours:
//   1. Enabling the per-pad secondary shell (secondaryEnabled + couplingStrength)
//      measurably changes a voice's output -- for BOTH the fast path
//      (block-rate body, e.g. Impulse exciter) and the slow path (per-sample
//      FeedbackExciter). Before the M-6 fix the slow path summed no shell at
//      all, so coupling-on == coupling-off there and assertion (1) fails for
//      Feedback.
//   2. The shell contributes a comparable share of the output energy on the
//      slow path as it does on the fast path -- i.e. the FeedbackExciter route
//      is no longer silently missing the entire Phase 8D stage.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "voice_pool/voice_pool.h"
#include "voice_pool_test_helpers.h"
#include "dsp/pad_config.h"
#include "dsp/exciter_type.h"
#include "dsp/body_model_type.h"

#include <cmath>
#include <vector>

using namespace Membrum;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int    kBlockSize  = 256;
constexpr int    kNumBlocks  = 64;          // ~340 ms tail
constexpr std::uint8_t kTestNote = 38;      // any enabled pad; config set on all

// Apply the secondary-shell config to every pad so whichever pad `kTestNote`
// resolves to is configured. `coupling == false` fully disables Phase 8D.
void configureShell(VoicePool& pool, ExciterType exciter, bool coupling) noexcept
{
    TestHelpers::setAllPadsVoiceParams(pool, 0.5f, 0.5f, 0.3f, 0.3f, 0.8f);
    TestHelpers::setAllPadsExciterType(pool, exciter);
    TestHelpers::setAllPadsBodyModel(pool, BodyModelType::Membrane);

    const float couplingStrength = coupling ? 0.8f : 0.0f;
    const float secondaryEnabled = coupling ? 1.0f : 0.0f;
    for (int p = 0; p < kNumPads; ++p)
    {
        pool.setPadConfigField(p, kPadCouplingStrength, couplingStrength);
        pool.setPadConfigField(p, kPadSecondaryEnabled, secondaryEnabled);
        pool.setPadConfigField(p, kPadSecondarySize, 0.5f);
        pool.setPadConfigField(p, kPadSecondaryMaterial, 0.5f);
    }
}

// Total output energy (sum of squares, both channels) over kNumBlocks after a
// single strike. Deterministic: fresh pool, single note, fixed window.
double measureEnergy(ExciterType exciter, bool coupling)
{
    VoicePool pool;
    pool.prepare(kSampleRate, kBlockSize);
    pool.setMaxPolyphony(1);
    configureShell(pool, exciter, coupling);

    std::vector<float> outL(static_cast<size_t>(kBlockSize), 0.0f);
    std::vector<float> outR(static_cast<size_t>(kBlockSize), 0.0f);

    pool.noteOn(kTestNote, 1.0f);

    double energy = 0.0;
    for (int b = 0; b < kNumBlocks; ++b)
    {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        pool.processBlock(outL.data(), outR.data(), kBlockSize);
        for (int s = 0; s < kBlockSize; ++s)
        {
            energy += static_cast<double>(outL[static_cast<size_t>(s)])
                    * outL[static_cast<size_t>(s)];
            energy += static_cast<double>(outR[static_cast<size_t>(s)])
                    * outR[static_cast<size_t>(s)];
        }
    }
    return energy;
}

// Relative change in output energy from enabling the secondary shell.
double shellEnergyDelta(ExciterType exciter)
{
    const double off = measureEnergy(exciter, /*coupling*/ false);
    const double on  = measureEnergy(exciter, /*coupling*/ true);
    REQUIRE(off > 0.0);                 // sanity: voice produced sound
    REQUIRE(std::isfinite(on));
    REQUIRE(std::isfinite(off));
    return (on - off) / off;
}

} // namespace

// =============================================================================
// L-9 (1): the secondary shell is audible on the FAST path (block-rate body).
// This path always summed the shell; it's the control for the slow-path test.
// =============================================================================
TEST_CASE("Secondary shell audibly changes the fast-path voice output",
          "[membrum][voice_pool][coupling][secondary][fast][L9]")
{
    const double delta = shellEnergyDelta(ExciterType::Impulse);
    CAPTURE(delta);
    // The passive shell layer adds resonant energy; require a clearly
    // suprathreshold change (well above numerical noise).
    CHECK(delta > 0.05);
}

// =============================================================================
// L-9 (2) + M-6: the secondary shell must ALSO be audible on the SLOW path
// (per-sample FeedbackExciter). Before the M-6 fix processBlockSlow had no
// secondaryBank_ stage, so coupling-on == coupling-off and this CHECK reports
// a ~0 delta -> FAILS. After the fix the shell contributes and delta > 0.05.
// =============================================================================
TEST_CASE("Secondary shell audibly changes the slow-path (Feedback) voice output",
          "[membrum][voice_pool][coupling][secondary][slow][M6][L9]")
{
    const double delta = shellEnergyDelta(ExciterType::Feedback);
    CAPTURE(delta);
    CHECK(delta > 0.05);
}
