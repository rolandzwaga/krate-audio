// ==============================================================================
// Phase 3.1 -- VoicePool::noteOn/noteOff/processBlock allocation behaviour
// ==============================================================================
// T3.1.1 -- satisfies FR-113, FR-114, FR-115, SC-020 subset.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "dsp/default_kit.h"
#include "voice_pool/voice_pool.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace {

constexpr double kTestSampleRate = 44100.0;
constexpr int    kTestBlockSize  = 256;

// -ffast-math-safe finite-sample check (tasks.md §NaN Detection Pattern).
inline bool isFiniteSample(float x) noexcept
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &x, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

} // namespace

TEST_CASE("VoicePool: 8 concurrent note-ons yield 8 active voices",
          "[membrum][voice_pool][phase3_1]")
{
    Membrum::VoicePool pool;
    pool.prepare(kTestSampleRate, kTestBlockSize);
    pool.setMaxPolyphony(16);

    std::vector<float> outL(kTestBlockSize, 0.0f);
    std::vector<float> outR(kTestBlockSize, 0.0f);

    for (int i = 0; i < 8; ++i)
        pool.noteOn(static_cast<std::uint8_t>(36 + i), 0.8f);

    // The allocator only flips a voice into its rendered state once we have
    // processed at least one block. Run a single block to let every voice
    // latch in.
    pool.processBlock(outL.data(), outR.data(), kTestBlockSize);

    REQUIRE(pool.getActiveVoiceCount() == 8);
}

namespace {

struct PoolRender
{
    std::vector<float> left;
    std::vector<float> right;
    long retiredAt = -1;  // end of the first block after which no voice was active
};

// Default kit, note 36 at velocity 0.9, 2 s at kTestBlockSize; a pool note-off
// at the start of block `noteOffBlock` when it is >= 0.
PoolRender renderKick(int noteOffBlock)
{
    auto pool = std::make_unique<Membrum::VoicePool>();
    pool->prepare(kTestSampleRate, kTestBlockSize);
    pool->setMaxPolyphony(16);
    Membrum::DefaultKit::apply(pool->padConfigsArray());

    const long total = static_cast<long>(2.0 * kTestSampleRate);
    PoolRender r;
    r.left.assign(static_cast<std::size_t>(total), 0.0f);
    r.right.assign(static_cast<std::size_t>(total), 0.0f);
    std::vector<float> l(kTestBlockSize, 0.0f);
    std::vector<float> rr(kTestBlockSize, 0.0f);

    pool->noteOn(36, 0.9f);
    long rendered = 0;
    for (int b = 0; rendered < total; ++b)
    {
        if (b == noteOffBlock)
            pool->noteOff(36);
        const int n = static_cast<int>(std::min<long>(kTestBlockSize, total - rendered));
        std::fill(l.begin(), l.end(), 0.0f);
        std::fill(rr.begin(), rr.end(), 0.0f);
        pool->processBlock(l.data(), rr.data(), n);
        std::copy(l.begin(), l.begin() + n, r.left.begin() + rendered);
        std::copy(rr.begin(), rr.begin() + n, r.right.begin() + rendered);
        rendered += n;
        if (r.retiredAt < 0 && !pool->isAnyVoiceActive())
            r.retiredAt = rendered;
    }
    return r;
}

// One past the last sample at which either channel reaches `bar`.
long endOfLevel(const PoolRender& r, float bar)
{
    long last = -1;
    for (std::size_t i = 0; i < r.left.size(); ++i)
    {
        if (std::fabs(r.left[i]) >= bar || std::fabs(r.right[i]) >= bar)
            last = static_cast<long>(i);
    }
    return last + 1;
}

} // namespace

// FR-114 (fix plan T2.9, ruling 2026-11-25 (a)): a MIDI note-off releases only
// a sustaining exciter; the hit itself plays in full. A render comparison, so
// the case fails if a note-off gates the amp envelope or damps the body.
TEST_CASE("VoicePool: note-off is a no-op for percussion voices",
          "[membrum][voice_pool][phase3_1]")
{
    const PoolRender a = renderKick(-1);
    const PoolRender b = renderKick(1);  // note-off one block after the note-on

    double sumA = 0.0;
    double sumD = 0.0;
    for (std::size_t i = 0; i < a.left.size(); ++i)
    {
        sumA += static_cast<double>(a.left[i]) * a.left[i]
              + static_cast<double>(a.right[i]) * a.right[i];
        const double dl = static_cast<double>(b.left[i]) - a.left[i];
        const double dr = static_cast<double>(b.right[i]) - a.right[i];
        sumD += dl * dl + dr * dr;
    }
    const double diffDb =
        (sumD <= 0.0) ? -300.0 : 10.0 * std::log10(sumD / std::max(sumA, 1.0e-30));
    INFO("difference between note-on alone and note-on + note-off after one block: "
         << diffDb << " dB re the note-on-only render");
    REQUIRE(sumA > 0.0);
    CHECK(diffDb <= -120.0);
}

// FR-114: with no note-off, the voice retires through its own auto-release
// (T1.13: output below -60 dBFS for 20 ms, then the voice's release), not
// through a host note-off.
TEST_CASE("VoicePool: voice naturally becomes inactive after amp envelope decays",
          "[membrum][voice_pool][phase3_1]")
{
    const PoolRender r = renderKick(-1);
    // The voice output (before pan) is quiet once both pool channels stay
    // below 1e-3 x the centre-pan gain; 0.5 bounds any centre pan law.
    const long loudEnd = endOfLevel(r, 1.0e-3f);
    const long quietBound = endOfLevel(r, 0.5e-3f);
    INFO("pool output last at -60 dBFS: " << loudEnd * 1000.0 / kTestSampleRate
         << " ms, last at -66 dBFS: " << quietBound * 1000.0 / kTestSampleRate
         << " ms; retired at " << r.retiredAt * 1000.0 / kTestSampleRate << " ms");
    REQUIRE(r.retiredAt > 0);
    // Not cut before its natural decay ...
    CHECK(r.retiredAt >= loudEnd);
    // ... and retired within the auto-release's hold (20 ms), one control-grid
    // step (32 samples), the amp envelope's 300 ms release and one block.
    const long bound = quietBound
                     + static_cast<long>(std::lround((0.020 + 0.300) * kTestSampleRate))
                     + 32 + kTestBlockSize;
    CHECK(r.retiredAt <= bound);
}

TEST_CASE("VoicePool: 500 ms of 8-voice output has no NaN/Inf samples",
          "[membrum][voice_pool][phase3_1]")
{
    Membrum::VoicePool pool;
    pool.prepare(kTestSampleRate, kTestBlockSize);
    pool.setMaxPolyphony(16);

    std::vector<float> outL(kTestBlockSize, 0.0f);
    std::vector<float> outR(kTestBlockSize, 0.0f);

    for (int i = 0; i < 8; ++i)
        pool.noteOn(static_cast<std::uint8_t>(36 + i), 0.7f);

    const int blocks500ms =
        (static_cast<int>(kTestSampleRate) / 2) / kTestBlockSize + 1;

    bool allFinite = true;
    for (int b = 0; b < blocks500ms; ++b)
    {
        pool.processBlock(outL.data(), outR.data(), kTestBlockSize);
        for (int i = 0; i < kTestBlockSize; ++i)
        {
            if (!isFiniteSample(outL[i]) || !isFiniteSample(outR[i]))
            {
                allFinite = false;
                break;
            }
        }
        if (!allFinite) break;
    }
    REQUIRE(allFinite);
}

TEST_CASE("VoicePool: maxPolyphony=1 with MIDI note 36 produces audible non-NaN output",
          "[membrum][voice_pool][phase3_1]")
{
    Membrum::VoicePool pool;
    pool.prepare(kTestSampleRate, kTestBlockSize);
    pool.setMaxPolyphony(1);

    std::vector<float> outL(kTestBlockSize, 0.0f);
    std::vector<float> outR(kTestBlockSize, 0.0f);

    pool.noteOn(36, 100.0f / 127.0f);
    pool.processBlock(outL.data(), outR.data(), kTestBlockSize);

    float peak = 0.0f;
    bool allFinite = true;
    for (int i = 0; i < kTestBlockSize; ++i)
    {
        if (!isFiniteSample(outL[i])) { allFinite = false; break; }
        const float a = std::fabs(outL[i]);
        if (a > peak) peak = a;
    }
    REQUIRE(allFinite);
    REQUIRE(peak > 0.001f);  // At least -60 dBFS to prove the path is live.
}
