// ==============================================================================
// Auto-release does not depend on the host block size (fix plan T1.13: MB-052)
// ==============================================================================
// VoicePool used to call DrumVoice::noteOff() after any host block whose peak
// was below 1e-3: a window of one host block. With small blocks that fired at a
// zero crossing of a loud low tone, and on the first block of a hit (the amp
// envelope restarts from 0 with a 0.5 ms attack), so a 1-sample host killed the
// hit at its onset. The voice now owns the detector: a run of quiet output
// samples held for a fixed time, armed after an onset grace period and acting
// at a fixed point of voice time, so the release starts at the same voice
// sample whatever the host block size.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "dsp/default_kit.h"
#include "voice_pool/voice_pool.h"

#include <krate/dsp/core/db_utils.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

namespace {

constexpr double kSr = 48000.0;
constexpr int kMaxBlock = 512;
constexpr long kTotal = static_cast<long>(3.0 * kSr);

struct HeldRender
{
    std::vector<float> left;
    long retiredAt = -1;  // end of the first block after which no voice was active
    bool allFinite = true;
};

// Default kit, note 45 (low tom) at `velocity`, no note-off, 3 s at `block`.
HeldRender renderHeld(float velocity, int block)
{
    auto pool = std::make_unique<Membrum::VoicePool>();
    pool->prepare(kSr, kMaxBlock);
    Membrum::DefaultKit::apply(pool->padConfigsArray());
    pool->noteOn(45, velocity);

    HeldRender r;
    r.left.assign(static_cast<std::size_t>(kTotal), 0.0f);
    std::vector<float> l(static_cast<std::size_t>(kMaxBlock), 0.0f);
    std::vector<float> rr(static_cast<std::size_t>(kMaxBlock), 0.0f);
    long rendered = 0;
    while (rendered < kTotal)
    {
        const int n = static_cast<int>(std::min<long>(block, kTotal - rendered));
        pool->processBlock(l.data(), rr.data(), n);
        for (int i = 0; i < n; ++i)
        {
            if (!Krate::DSP::detail::isFinite(l[static_cast<std::size_t>(i)]))
                r.allFinite = false;
            r.left[static_cast<std::size_t>(rendered + i)] = l[static_cast<std::size_t>(i)];
        }
        rendered += n;
        if (r.retiredAt < 0 && !pool->isAnyVoiceActive())
            r.retiredAt = rendered;
    }
    return r;
}

double rmsDb(const std::vector<float>& x, double fromS, double toS)
{
    const auto a = static_cast<std::size_t>(std::lround(fromS * kSr));
    const auto b = static_cast<std::size_t>(std::lround(toS * kSr));
    double sum = 0.0;
    for (std::size_t i = a; i < b; ++i)
        sum += static_cast<double>(x[i]) * static_cast<double>(x[i]);
    const double ms = sum / static_cast<double>(b - a);
    return 10.0 * std::log10(std::max(ms, 1.0e-30));
}

} // namespace

TEST_CASE("Auto-release does not depend on the host block size", "[voice_pool][release][timing]")
{
    for (const float velocity : {0.25f, 0.8f})
    {
        const HeldRender ref = renderHeld(velocity, 512);
        REQUIRE(ref.allFinite);
        REQUIRE(ref.retiredAt > 0);
        const double refEarly = rmsDb(ref.left, 0.0, 0.05);
        const double refTail  = rmsDb(ref.left, 0.3, 0.6);

        for (const int block : {1, 16, 32, 64})
        {
            const HeldRender r = renderHeld(velocity, block);
            const double early = rmsDb(r.left, 0.0, 0.05);
            const double tail  = rmsDb(r.left, 0.3, 0.6);
            INFO("velocity " << velocity << ", block " << block << ": retired at "
                             << r.retiredAt * 1000.0 / kSr << " ms (block 512: "
                             << ref.retiredAt * 1000.0 / kSr << " ms); 0-50 ms RMS " << early
                             << " dBFS (block 512: " << refEarly << "); 300-600 ms RMS " << tail
                             << " dBFS (block 512: " << refTail << ")");
            REQUIRE(r.allFinite);
            REQUIRE(r.retiredAt > 0);
            // Within 50 ms (the fix plan's bar).
            CHECK(std::abs(static_cast<double>(r.retiredAt - ref.retiredAt)) <= 0.05 * kSr);
            CHECK(std::abs(early - refEarly) <= 1.0);
            CHECK(std::abs(tail - refTail) <= 1.0);
        }
    }
}
