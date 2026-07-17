// ==============================================================================
// Mallet body-size contact hint -- 06-orchestralKit-fix-plan.md D4
// ==============================================================================
// MalletExciter hardcoded mass 0.3 -> base Hertzian contact ~9.5 ms (3.8 ms at
// v=1). An ~8 ms Hann-like pulse has a spectral null region near ~244 Hz and
// couples terribly into small metal bodies (crotales/triangle/bell tree need
// ~0.5-2 ms contacts). D4 passes a body-size hint from DrumVoice so the
// effective mallet mass -- and hence contact time -- scales with the struck
// body, WITHOUT touching the velocity mapping (velocity-mapped mass was tried
// and reverted: it broke the 10 ms centroid window / velocity-ratio tests,
// see mallet_exciter.h history note).
//
//   (a) default hint (never wired) is bit-identical to the old fixed mass;
//   (b) a crotale-sized hint shortens the contact pulse decisively;
//   (c) a large-body hint keeps the long soft contact (timpani unchanged-ish).
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "dsp/exciters/mallet_exciter.h"

#include <cmath>
#include <vector>

namespace {

constexpr double kSampleRate = 44100.0;

// Samples from trigger until the excitation envelope last exceeds the
// threshold (pulse + its noise component).
int activeLengthSamples(Membrum::MalletExciter& ex, float velocity)
{
    ex.trigger(velocity);
    const int n = static_cast<int>(kSampleRate / 2);
    int last = 0;
    for (int i = 0; i < n; ++i)
    {
        const float s = ex.process(0.0f);
        if (std::fabs(s) > 1e-4f)
            last = i;
    }
    return last;
}

} // namespace

TEST_CASE("Mallet default hint reproduces the legacy fixed-mass pulse", "[exciters][mallet][D4]")
{
    // Two exciters, same voiceId (same RNG stream): one never touched, one
    // explicitly set to the neutral hint. Pulse lengths must match exactly.
    Membrum::MalletExciter untouched;
    untouched.prepare(kSampleRate, 42);
    Membrum::MalletExciter neutral;
    neutral.prepare(kSampleRate, 42);
    neutral.setBodySizeHint(1.0f);

    const int a = activeLengthSamples(untouched, 0.5f);
    const int b = activeLengthSamples(neutral, 0.5f);
    REQUIRE(a == b);
}

TEST_CASE("Crotale-sized hint shortens the mallet contact decisively", "[exciters][mallet][D4]")
{
    Membrum::MalletExciter big;
    big.prepare(kSampleRate, 42);
    big.setBodySizeHint(1.0f);

    Membrum::MalletExciter small;
    small.prepare(kSampleRate, 42);
    small.setBodySizeHint(0.12f);   // crotales-hi size

    const int lenBig   = activeLengthSamples(big, 1.0f);
    const int lenSmall = activeLengthSamples(small, 1.0f);

    INFO("big " << lenBig << " samples (" << 1000.0 * lenBig / kSampleRate
         << " ms), small " << lenSmall << " samples ("
         << 1000.0 * lenSmall / kSampleRate << " ms)");
    // ~3.5 ms class vs ~1.8 ms class at v=1 (measured envelope includes a
    // shared SVF/noise tail past the Hertzian T, which compresses the ratio):
    // require at least a 1.5x shortening plus the absolute class bound below.
    REQUIRE(lenSmall * 3 < lenBig * 2);
    // And the small contact lands in the small-metal class (< 2 ms, the
    // 0.5-2 ms range the D4 finding targets).
    REQUIRE(lenSmall < static_cast<int>(kSampleRate * 0.002));
}

TEST_CASE("Drum-sized hints keep the legacy soft mallet contact exactly", "[exciters][mallet][D4]")
{
    // Above the 0.5 knee the effective mass is the legacy 0.3 EXACTLY --
    // toms/timpani/gran cassa contacts (and the tension-glide calibration
    // that depends on their contact energy) are untouched. Same voiceId =
    // same RNG micro-variation stream, so equal mass => equal pulse length.
    for (const float hint : {0.5f, 0.68f, 0.90f})
    {
        Membrum::MalletExciter fixed;
        fixed.prepare(kSampleRate, 42);
        fixed.setBodySizeHint(1.0f);

        Membrum::MalletExciter drum;
        drum.prepare(kSampleRate, 42);
        drum.setBodySizeHint(hint);

        const int lenFixed = activeLengthSamples(fixed, 0.5f);
        const int lenDrum  = activeLengthSamples(drum, 0.5f);
        INFO("hint " << hint << ": fixed " << lenFixed << ", drum " << lenDrum);
        REQUIRE(lenDrum == lenFixed);
    }
}
