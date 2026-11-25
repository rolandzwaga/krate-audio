// ==============================================================================
// M-9 -- Per-pad equal-power pan
// ==============================================================================
// Audit finding M-9 (AUDIT-signal-path-2026-06-07.md): the voice/pool path was
// mono -- every voice was duplicated to L and R with unity gain, so all 32 pads
// sat dead-center. M-9 adds a per-pad pan field + an equal-power pan law in the
// pool's mix stage.
//
// Law (VoicePool::equalPowerPanGains): pan in [0, 1] normalized, 0.5 = center.
//   theta  = pan * pi/2
//   gainL  = sqrt(2) * cos(theta)
//   gainR  = sqrt(2) * sin(theta)
// The sqrt(2) lifts the textbook -3 dB equal-power center back to 0 dB. The
// center itself is a branch (T2.27): gainL == gainR == 1.0 exactly, so a
// default (pan 0.5) pad is BIT-IDENTICAL to the legacy mono duplication.
// Constant power: gainL^2 + gainR^2 == 2 for all pan, so a hard pan puts
// +3 dB into one channel of the bus limiter.
//
// Assertions:
//   - center (0.5): outL == outR sample-for-sample (legacy mono-dup), energy>0.
//   - hard left (0.0): R channel is exactly silent; L carries the energy.
//   - hard right (1.0): L channel is exactly silent; R carries the energy.
//   - constant power: total energy (L+R) is pan-independent (the mono source is
//     identical across renders), and a hard-panned channel carries the full
//     mono power (== center L + center R).
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "voice_pool/voice_pool.h"
#include "voice_pool_test_helpers.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int    kBlockSize  = 128;
constexpr int    kBlocks     = 8;   // ~23 ms -- well into the body ring

struct ChannelEnergy
{
    double eL = 0.0;
    double eR = 0.0;
    std::vector<float> outL;
    std::vector<float> outR;
};

// Render a single pad-0 voice at the given normalized pan and capture the L/R
// energy plus the raw stereo stream. Every render uses a fresh pool so the
// mono source (exciter PRNG / body) is identical across pan values.
ChannelEnergy renderPan(float panNorm)
{
    Membrum::VoicePool pool;
    pool.prepare(kSampleRate, kBlockSize);
    pool.setVoiceStealingPolicy(Membrum::VoiceStealingPolicy::Oldest);
    Membrum::TestHelpers::setAllPadsVoiceParams(pool, 0.5f, 0.5f, 0.3f, 0.3f, 0.8f);
    Membrum::TestHelpers::setAllPadsExciterType(pool, Membrum::ExciterType::Impulse);
    Membrum::TestHelpers::setAllPadsBodyModel(pool, Membrum::BodyModelType::Membrane);
    pool.setPadConfigField(0, Membrum::kPadPan, panNorm);

    std::vector<float> outL(static_cast<std::size_t>(kBlockSize), 0.0f);
    std::vector<float> outR(static_cast<std::size_t>(kBlockSize), 0.0f);

    pool.noteOn(36, 0.9f);  // pad 0

    ChannelEnergy r;
    for (int b = 0; b < kBlocks; ++b)
    {
        pool.processBlock(outL.data(), outR.data(), kBlockSize);
        for (int i = 0; i < kBlockSize; ++i)
        {
            r.outL.push_back(outL[i]);
            r.outR.push_back(outR[i]);
            r.eL += static_cast<double>(outL[i]) * outL[i];
            r.eR += static_cast<double>(outR[i]) * outR[i];
        }
    }
    return r;
}

} // namespace

TEST_CASE("VoicePool per-pad pan: center is the legacy mono duplication (M-9)",
          "[membrum][voice_pool][pan][m9]")
{
    const ChannelEnergy c = renderPan(0.5f);

    REQUIRE(c.eL > 0.0);
    // Center: both channels are bit-identical to the mono voice output.
    REQUIRE(c.outL.size() == c.outR.size());
    for (std::size_t i = 0; i < c.outL.size(); ++i)
        REQUIRE(c.outL[i] == c.outR[i]);
    // Equal energy on both sides.
    REQUIRE(c.eL == Catch::Approx(c.eR).margin(1e-12));
}

TEST_CASE("VoicePool per-pad pan: hard left/right silence the opposite channel (M-9)",
          "[membrum][voice_pool][pan][m9]")
{
    const ChannelEnergy left  = renderPan(0.0f);
    const ChannelEnergy right = renderPan(1.0f);

    REQUIRE(left.eL > 0.0);
    REQUIRE(right.eR > 0.0);

    // Hard left: the right channel is silent. The opposite-channel gain is
    // sqrt(2)*sin/cos at the [0, pi/2] extremes; sin(0)==0 exactly while
    // cos(pi/2) in float is ~-4.4e-8 (float pi/2 != true pi/2), so the leak
    // is at worst ~-156 dBFS. Assert it is >= 120 dB below the active channel
    // (energy ratio < 1e-12) rather than bit-exact zero.
    REQUIRE(left.eR  < 1e-12 * left.eL);
    REQUIRE(right.eL < 1e-12 * right.eR);
}

TEST_CASE("VoicePool per-pad pan: constant power across pan positions (M-9)",
          "[membrum][voice_pool][pan][m9]")
{
    const ChannelEnergy center = renderPan(0.5f);
    const ChannelEnergy left    = renderPan(0.0f);
    const ChannelEnergy right   = renderPan(1.0f);

    const double centerTotal = center.eL + center.eR;
    const double leftTotal    = left.eL + left.eR;
    const double rightTotal   = right.eL + right.eR;

    // Total power (L^2 + R^2) is pan-independent: the mono source is identical
    // across renders and gainL^2 + gainR^2 == 2 for all pan. The tolerance is
    // float32 sample precision (the gains are sqrt(2)*cos/sin, ~1.0 but not
    // bit-exact, so per-sample float rounding accumulates ~1e-7 over the
    // ~1000-sample energy sum); a wrong law (e.g. linear pan) is off by ~2x.
    REQUIRE(leftTotal  == Catch::Approx(centerTotal).epsilon(1e-4));
    REQUIRE(rightTotal == Catch::Approx(centerTotal).epsilon(1e-4));

    // A hard-panned channel carries the FULL mono power == both center channels.
    REQUIRE(left.eL  == Catch::Approx(centerTotal).epsilon(1e-4));
    REQUIRE(right.eR == Catch::Approx(centerTotal).epsilon(1e-4));
}

// T2.27 (MB-066): the centre gains are exactly unity (a branch, not
// sqrt(2) * cos(pi/4), which is 0.99999994 in float), and the law keeps
// constant power gainL^2 + gainR^2 == 2 off centre.
TEST_CASE("VoicePool per-pad pan: centre gains are exactly unity", "[voice_pool][pan]")
{
    float gainL = 0.0f;
    float gainR = 0.0f;
    Membrum::VoicePool::equalPowerPanGains(0.5f, gainL, gainR);
    CAPTURE(gainL, gainR);
    CHECK(gainL == 1.0f);
    CHECK(gainR == 1.0f);

    for (const float pan : {0.0f, 0.25f, 0.75f, 1.0f})
    {
        Membrum::VoicePool::equalPowerPanGains(pan, gainL, gainR);
        const double power = static_cast<double>(gainL) * gainL
                           + static_cast<double>(gainR) * gainR;
        CAPTURE(pan, gainL, gainR, power);
        CHECK(std::abs(power - 2.0) <= 1e-6);
    }
}

// T2.25 (MB-056): the pan is latched at note-on. A mid-ring pan edit leaves
// the ringing voice where it started (no per-block gain step); the pad's next
// note-on plays at the new pan.
TEST_CASE("VoicePool per-pad pan: a ringing voice keeps its note-on pan",
          "[voice_pool][pan]")
{
    Membrum::VoicePool pool;
    pool.prepare(kSampleRate, kBlockSize);
    pool.setVoiceStealingPolicy(Membrum::VoiceStealingPolicy::Oldest);
    Membrum::TestHelpers::setAllPadsVoiceParams(pool, 0.5f, 0.5f, 0.3f, 0.3f, 0.8f);
    Membrum::TestHelpers::setAllPadsExciterType(pool, Membrum::ExciterType::Impulse);
    Membrum::TestHelpers::setAllPadsBodyModel(pool, Membrum::BodyModelType::Membrane);
    pool.setPadConfigField(0, Membrum::kPadPan, 0.5f);

    std::vector<float> outL(static_cast<std::size_t>(kBlockSize), 0.0f);
    std::vector<float> outR(static_cast<std::size_t>(kBlockSize), 0.0f);
    const auto energy = [](const std::vector<float>& v)
    {
        double e = 0.0;
        for (const float s : v)
            e += static_cast<double>(s) * s;
        return e;
    };

    pool.noteOn(36, 1.0f);
    for (int b = 0; b < 4; ++b)
        pool.processBlock(outL.data(), outR.data(), kBlockSize);

    // Mid-ring: hard left. The ringing voice stays centred.
    pool.setPadConfigField(0, Membrum::kPadPan, 0.0f);
    pool.processBlock(outL.data(), outR.data(), kBlockSize);
    REQUIRE(energy(outL) > 0.0);
    bool centred = true;
    for (std::size_t i = 0; i < outL.size(); ++i)
        centred = centred && (outL[i] == outR[i]);
    CHECK(centred);

    // Let the voice finish (bounded: 20 s of audio), then strike again: the
    // new hit is hard left.
    const int maxBlocks = static_cast<int>(20.0 * kSampleRate) / kBlockSize;
    int blocks = 0;
    while (pool.isAnyVoiceActive() && blocks < maxBlocks)
    {
        pool.processBlock(outL.data(), outR.data(), kBlockSize);
        ++blocks;
    }
    REQUIRE_FALSE(pool.isAnyVoiceActive());

    pool.noteOn(36, 1.0f);
    pool.processBlock(outL.data(), outR.data(), kBlockSize);
    CHECK(energy(outL) > 0.0);
    CHECK(energy(outR) == 0.0);
}
