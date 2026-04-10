// ==============================================================================
// Material Morph tests -- Phase 8, T106
// ==============================================================================
// Covers unnatural_zone_contract.md "Material Morph" section and FR-054.
//
// (a) enabled start=1 end=0 duration=500 ms -> monotonic change.
// (b) disabled -> static Material throughout.
// (c) short duration (< one process block) -> no hang, no static timbre.
// (d) duration=0 -> no divide-by-zero; returns static material.
// (e) allocation-free.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/unnatural/material_morph.h"

#include <allocation_detector.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

using Catch::Approx;

namespace {

constexpr double kSampleRate = 44100.0;

inline bool isFiniteSample(float x) noexcept
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &x, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

} // namespace

// ==============================================================================
// T106(a) -- enabled, start=1, end=0, duration=500 ms: the envelope must
// change monotonically from start to end.
// ==============================================================================

TEST_CASE("UnnaturalZone MaterialMorph -- linear envelope changes monotonically",
          "[UnnaturalZone][MaterialMorph]")
{
    Membrum::MaterialMorph morph;
    morph.prepare(kSampleRate);
    morph.setEnabled(true);
    morph.setStart(1.0f);
    morph.setEnd(0.0f);
    morph.setDurationMs(500.0f);
    morph.setCurve(false); // linear

    morph.trigger();

    const int numSamples = static_cast<int>(0.6 * kSampleRate); // 600 ms
    std::vector<float> values(static_cast<std::size_t>(numSamples), 0.0f);
    for (int i = 0; i < numSamples; ++i)
        values[static_cast<std::size_t>(i)] = morph.process();

    // All finite.
    bool allFinite = true;
    for (float v : values) if (!isFiniteSample(v)) { allFinite = false; break; }
    CHECK(allFinite);

    // Start near 1.0, end near 0.0.
    CHECK(values[0] == Approx(1.0f).margin(0.05f));
    CHECK(values.back() == Approx(0.0f).margin(0.05f));

    // Monotonic non-increasing during the morph region (first 500 ms).
    const int morphEnd = static_cast<int>(0.5 * kSampleRate);
    bool monotonic = true;
    for (int i = 1; i < morphEnd; ++i)
    {
        if (values[static_cast<std::size_t>(i)] >
            values[static_cast<std::size_t>(i - 1)] + 1e-5f)
        {
            monotonic = false;
            INFO("non-monotonic at i=" << i
                 << " prev=" << values[static_cast<std::size_t>(i - 1)]
                 << " cur=" << values[static_cast<std::size_t>(i)]);
            break;
        }
    }
    CHECK(monotonic);
}

// ==============================================================================
// T106(b) -- disabled -> static Material.
// ==============================================================================

TEST_CASE("UnnaturalZone MaterialMorph -- disabled yields static material",
          "[UnnaturalZone][MaterialMorph][DefaultsOff]")
{
    Membrum::MaterialMorph morph;
    morph.prepare(kSampleRate);
    morph.setEnabled(false);
    morph.setStart(0.8f);
    morph.setEnd(0.2f);
    morph.setDurationMs(300.0f);

    morph.trigger();

    for (int i = 0; i < 1024; ++i)
    {
        const float v = morph.process();
        CHECK(v == Approx(0.8f).margin(1e-6f));
    }
    CHECK(!morph.isActive());
}

// ==============================================================================
// T106(c) -- duration shorter than a process block (e.g. 1 ms on a 10 ms block)
// -> morph completes without hanging or producing a static timbre.
// ==============================================================================

TEST_CASE("UnnaturalZone MaterialMorph -- sub-block duration completes",
          "[UnnaturalZone][MaterialMorph]")
{
    Membrum::MaterialMorph morph;
    morph.prepare(kSampleRate);
    morph.setEnabled(true);
    morph.setStart(1.0f);
    morph.setEnd(0.0f);
    morph.setDurationMs(1.0f); // 44 samples @ 44.1 kHz
    morph.setCurve(false);

    morph.trigger();

    // Run 128 samples (enough to cover the 44-sample morph).
    std::array<float, 128> values{};
    for (int i = 0; i < 128; ++i) values[static_cast<std::size_t>(i)] = morph.process();

    CHECK(values[0] == Approx(1.0f).margin(0.05f));
    // After a few blocks we should be holding at end (0.0).
    CHECK(values[127] == Approx(0.0f).margin(0.05f));
    // No NaN/Inf.
    for (float v : values) CHECK(isFiniteSample(v));
}

// ==============================================================================
// T106(d) -- duration = 0 must not divide by zero; returns static material.
// ==============================================================================

TEST_CASE("UnnaturalZone MaterialMorph -- duration==0 does not divide by zero",
          "[UnnaturalZone][MaterialMorph]")
{
    Membrum::MaterialMorph morph;
    morph.prepare(kSampleRate);
    morph.setEnabled(true);
    morph.setStart(0.7f);
    morph.setEnd(0.2f);
    morph.setDurationMs(0.0f);

    morph.trigger();

    for (int i = 0; i < 100; ++i)
    {
        const float v = morph.process();
        // Expected: stays at start because totalSamples_ == 0 -> disabled
        // branch returns startMaterial_.
        CHECK(isFiniteSample(v));
        CHECK(v == Approx(0.7f).margin(1e-6f));
    }
}

// ==============================================================================
// T106(e) -- Allocation detector: process() + trigger() are zero-allocation.
// ==============================================================================

TEST_CASE("UnnaturalZone MaterialMorph -- zero heap allocations on audio thread",
          "[UnnaturalZone][MaterialMorph][allocation]")
{
    Membrum::MaterialMorph morph;
    morph.prepare(kSampleRate);
    morph.setEnabled(true);
    morph.setStart(1.0f);
    morph.setEnd(0.0f);
    morph.setDurationMs(500.0f);

    // Warm up.
    morph.trigger();
    for (int i = 0; i < 128; ++i) (void) morph.process();

    {
        TestHelpers::AllocationScope scope;
        morph.trigger();
        for (int i = 0; i < 4096; ++i) (void) morph.process();
        const size_t count = scope.getAllocationCount();
        INFO("MaterialMorph process/trigger alloc count = " << count);
        CHECK(count == 0u);
    }
}
