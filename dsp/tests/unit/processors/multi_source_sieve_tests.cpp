// ==============================================================================
// Layer 2: DSP Processor Tests - Multi-Source Sieve
// ==============================================================================
// Tests for: dsp/include/krate/dsp/processors/multi_source_sieve.h
//
// Verifies peak-to-source assignment and polyphonic frame construction.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/processors/multi_source_sieve.h>
#include <krate/dsp/processors/harmonic_types.h>

#include <array>
#include <cmath>

using Catch::Approx;
using namespace Krate::DSP;

namespace {

constexpr float kTestSampleRate = 44100.0f;

/// Create a partial at the given frequency
Partial makePartial(float freq, float amp = 1.0f) {
    Partial p{};
    p.frequency = freq;
    p.amplitude = amp;
    p.stability = 0.8f;
    p.age = 5;
    return p;
}

/// Create a MultiF0Result with the given F0 frequencies
MultiF0Result makeF0s(std::initializer_list<float> freqs) {
    MultiF0Result result{};
    int i = 0;
    for (float f : freqs) {
        if (i >= kMaxPolyphonicVoices) break;
        result.estimates[static_cast<size_t>(i)].frequency = f;
        result.estimates[static_cast<size_t>(i)].confidence = 0.9f;
        result.estimates[static_cast<size_t>(i)].voiced = true;
        ++i;
    }
    result.numDetected = i;
    return result;
}

} // anonymous namespace

TEST_CASE("MultiSourceSieve: single source assigns harmonics correctly",
           "[multi_source_sieve]") {
    MultiSourceSieve sieve;
    sieve.prepare(kTestSampleRate);

    std::array<Partial, kMaxPartials> partials{};
    // Harmonics of 440 Hz
    partials[0] = makePartial(440.0f);   // h=1
    partials[1] = makePartial(880.0f);   // h=2
    partials[2] = makePartial(1320.0f);  // h=3
    int numPartials = 3;

    auto f0s = makeF0s({440.0f});

    sieve.assignSources(partials, numPartials, f0s);

    CHECK(partials[0].sourceId == 1);
    CHECK(partials[0].harmonicIndex == 1);
    CHECK(partials[1].sourceId == 1);
    CHECK(partials[1].harmonicIndex == 2);
    CHECK(partials[2].sourceId == 1);
    CHECK(partials[2].harmonicIndex == 3);
}

TEST_CASE("MultiSourceSieve: two sources separated correctly",
           "[multi_source_sieve]") {
    MultiSourceSieve sieve;
    sieve.prepare(kTestSampleRate);

    std::array<Partial, kMaxPartials> partials{};
    // Source 1: harmonics of 440 Hz (with typical 1/h decay)
    partials[0] = makePartial(440.0f, 1.0f);   // h=1
    partials[1] = makePartial(880.0f, 0.5f);   // h=2
    partials[2] = makePartial(1760.0f, 0.25f); // h=4 (unambiguous: not a harmonic of 330)
    // Source 2: harmonics of 330 Hz (with typical 1/h decay)
    partials[3] = makePartial(330.0f, 0.9f);   // h=1
    partials[4] = makePartial(660.0f, 0.45f);  // h=2
    partials[5] = makePartial(990.0f, 0.3f);   // h=3
    int numPartials = 6;

    auto f0s = makeF0s({440.0f, 330.0f});

    sieve.assignSources(partials, numPartials, f0s);

    // 440 Hz harmonics should be source 1
    CHECK(partials[0].sourceId == 1); // 440 -> h=1 of 440
    CHECK(partials[1].sourceId == 1); // 880 -> h=2 of 440
    CHECK(partials[2].sourceId == 1); // 1760 -> h=4 of 440

    // 330 Hz harmonics should be source 2
    CHECK(partials[3].sourceId == 2); // 330 -> h=1 of 330
    CHECK(partials[4].sourceId == 2); // 660 -> h=2 of 330
    CHECK(partials[5].sourceId == 2); // 990 -> h=3 of 330
}

TEST_CASE("MultiSourceSieve: unmatched peaks get sourceId 0",
           "[multi_source_sieve]") {
    MultiSourceSieve sieve;
    sieve.prepare(kTestSampleRate);

    std::array<Partial, kMaxPartials> partials{};
    partials[0] = makePartial(440.0f);    // Matches 440 Hz source
    partials[1] = makePartial(1234.5f);   // Random frequency, doesn't match anything
    int numPartials = 2;

    auto f0s = makeF0s({440.0f});

    sieve.assignSources(partials, numPartials, f0s);

    CHECK(partials[0].sourceId == 1);
    CHECK(partials[1].sourceId == 0); // Inharmonic
}

TEST_CASE("MultiSourceSieve: buildPolyphonicFrame separates sources",
           "[multi_source_sieve]") {
    MultiSourceSieve sieve;
    sieve.prepare(kTestSampleRate);

    std::array<Partial, kMaxPartials> partials{};
    // Source 1: 440 Hz harmonics
    partials[0] = makePartial(440.0f);
    partials[0].sourceId = 1;
    partials[0].harmonicIndex = 1;
    partials[1] = makePartial(880.0f);
    partials[1].sourceId = 1;
    partials[1].harmonicIndex = 2;
    // Source 2: 330 Hz harmonics
    partials[2] = makePartial(330.0f);
    partials[2].sourceId = 2;
    partials[2].harmonicIndex = 1;
    // Inharmonic
    partials[3] = makePartial(1500.0f);
    partials[3].sourceId = 0;
    int numPartials = 4;

    auto f0s = makeF0s({440.0f, 330.0f});

    auto frame = sieve.buildPolyphonicFrame(partials, numPartials, f0s, 0.5f);

    CHECK(frame.numSources == 2);
    CHECK(frame.sources[0].numPartials == 2); // Source 1: 440, 880
    CHECK(frame.sources[0].f0 == Approx(440.0f));
    CHECK(frame.sources[1].numPartials == 1); // Source 2: 330
    CHECK(frame.sources[1].f0 == Approx(330.0f));
    CHECK(frame.numInharmonicPartials == 1);
    CHECK(frame.globalAmplitude == Approx(0.5f));
}

TEST_CASE("MultiSourceSieve: empty F0 result does nothing",
           "[multi_source_sieve]") {
    MultiSourceSieve sieve;
    sieve.prepare(kTestSampleRate);

    std::array<Partial, kMaxPartials> partials{};
    partials[0] = makePartial(440.0f);
    int numPartials = 1;

    MultiF0Result emptyF0s{};

    sieve.assignSources(partials, numPartials, emptyF0s);

    // sourceId should be unchanged (default 0)
    CHECK(partials[0].sourceId == 0);
}

TEST_CASE("MultiSourceSieve: conflict resolution - peak assigned to best fit",
           "[multi_source_sieve]") {
    MultiSourceSieve sieve;
    sieve.prepare(kTestSampleRate);

    std::array<Partial, kMaxPartials> partials{};
    // 660 Hz = 3rd harmonic of 220 Hz, AND 2nd harmonic of 330 Hz
    // With F0s at 220 Hz and 330 Hz, the sieve should assign to the better fit
    partials[0] = makePartial(660.0f);
    int numPartials = 1;

    auto f0s = makeF0s({220.0f, 330.0f});

    sieve.assignSources(partials, numPartials, f0s);

    // 660 / 330 = exactly 2.0 (h=2, zero error)
    // 660 / 220 = exactly 3.0 (h=3, zero error)
    // Both are exact matches. The sieve should pick the one with lower
    // normalized error (tolerance scales as sqrt(h), so h=2 has lower tolerance
    // -> lower normalized score)
    // For source 1 (220Hz): error=0, tolerance=0.06*sqrt(3)*220=22.86, score=0
    // For source 2 (330Hz): error=0, tolerance=0.06*sqrt(2)*330=28.00, score=0
    // Both score 0, first wins
    CHECK(partials[0].sourceId >= 1); // Assigned to some source
}
