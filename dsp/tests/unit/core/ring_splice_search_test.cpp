// ==============================================================================
// findRingSpliceMatch against its frozen original, bit for bit
// ==============================================================================
// PitchSyncGranularShifter::beginSplice searched with `buffer_[idx % n]` per sample (two 64-bit divisions per sample
// pair: ~93 % of the Iterum Freeze Harmonic Drones render, row P11). findRingSpliceMatch indexes without division and
// scores four candidates per pass, keeping each candidate's accumulation order. This TU holds the original search
// verbatim and asserts that both choose the same offset with the same score, bit for bit, over the shifter's buffer
// geometry (44.1 and 96 kHz), both directions, windows wrapping past index 0, noise, periodic input (near-ties) and
// silence. A bit-identity claim between two separately compiled computations: it lives in dsp_ieee_tests.

#include <krate/dsp/core/ring_splice_search.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

namespace {

struct Frozen {
    bool found = false;
    std::size_t offset = 0;
    double score = -2.0;
};

// PitchSyncGranularShifter::beginSplice's search as of 5faff368e (pitch_shift_processor.cpp), verbatim but for the
// returned struct.
Frozen frozenSearch(const std::vector<float>& buffer_, std::size_t base, bool older, std::size_t minOffset,
                    std::size_t maxOffset, std::size_t matchLength) {
    const std::size_t n = buffer_.size();
    auto at = [&](std::size_t offset, std::size_t back) {
        const std::size_t idx = older ? (base + 2 * n - offset - back) : (base + n + offset - back);
        return buffer_[idx % n];
    };

    double outgoingEnergy = 0.0;
    for (std::size_t j = 0; j < matchLength; ++j) {
        const float x = at(0, j);
        outgoingEnergy += static_cast<double>(x) * x;
    }
    if (outgoingEnergy <= 1e-12) return {};

    std::size_t bestOffset = maxOffset;
    double bestScore = -2.0;
    for (std::size_t offset = minOffset; offset <= maxOffset; ++offset) {
        double dot = 0.0;
        double energy = 0.0;
        for (std::size_t j = 0; j < matchLength; ++j) {
            const float x = at(0, j);
            const float y = at(offset, j);
            dot += static_cast<double>(x) * y;
            energy += static_cast<double>(y) * y;
        }
        const double score = (energy > 1e-12) ? dot / std::sqrt(outgoingEnergy * energy) : -1.0;
        if (score > bestScore) {
            bestScore = score;
            bestOffset = offset;
        }
    }
    return {true, bestOffset, bestScore};
}

enum class Fill : std::uint8_t { Noise, Sine, SineWithNoise, Sparse, Silence };

std::vector<float> makeBuffer(std::size_t n, Fill fill, std::mt19937& rng) {
    std::vector<float> b(n, 0.0f);
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    std::uniform_real_distribution<double> period(30.0, 600.0);
    const double p = period(rng);
    for (std::size_t i = 0; i < n; ++i) {
        const auto s = static_cast<float>(0.5 * std::sin(6.283185307179586 * static_cast<double>(i) / p));
        switch (fill) {
            case Fill::Noise: b[i] = u(rng); break;
            case Fill::Sine: b[i] = s; break;
            case Fill::SineWithNoise: b[i] = s + 1e-3f * u(rng); break;
            case Fill::Sparse: b[i] = (i % 97 == 0) ? u(rng) : 0.0f; break;
            case Fill::Silence: b[i] = 0.0f; break;
        }
    }
    return b;
}

}  // namespace

TEST_CASE("findRingSpliceMatch chooses the frozen search's offset and score bit for bit", "[core][ring_splice]") {
    std::mt19937 rng(0x5EED1234u);
    int compared = 0;
    int wrapped = 0;
    int silent = 0;
    for (const double sampleRate : {44100.0, 96000.0}) {
        // PitchSyncGranularShifter::prepare: maxGrainSamples_ = 30 ms, bufferSize_ = 4 * maxGrain + 64.
        const auto maxGrain = static_cast<std::size_t>(30.0f * 0.001f * static_cast<float>(sampleRate));
        const std::size_t n = maxGrain * 4 + 64;
        for (const Fill fill : {Fill::Noise, Fill::Sine, Fill::SineWithNoise, Fill::Sparse, Fill::Silence}) {
            const auto buffer = makeBuffer(n, fill, rng);
            for (int trial = 0; trial < 60; ++trial) {
                std::uniform_int_distribution<std::size_t> anyBase(0, n - 1);
                std::uniform_int_distribution<std::size_t> nearZero(0, 300);
                std::uniform_int_distribution<std::size_t> nearEnd(n - 300, n - 1);
                std::size_t base = 0;
                if (trial % 3 == 0) {
                    base = nearZero(rng);  // windows that wrap past index 0
                } else if (trial % 6 == 1) {
                    base = nearEnd(rng);  // newer candidates whose ends straddle the buffer end
                } else {
                    base = anyBase(rng);
                }
                const bool older = (trial % 2) == 0;
                std::uniform_int_distribution<std::size_t> match(1, Krate::DSP::detail::kRingSpliceMaxMatch);
                const std::size_t matchLength = (trial % 5 == 0) ? Krate::DSP::detail::kRingSpliceMaxMatch : match(rng);
                std::uniform_int_distribution<std::size_t> lo(1, maxGrain);
                std::size_t minOffset = lo(rng);
                std::size_t maxOffset = lo(rng);
                if (minOffset > maxOffset) std::swap(minOffset, maxOffset);

                const Frozen want = frozenSearch(buffer, base, older, minOffset, maxOffset, matchLength);
                const auto got = Krate::DSP::detail::findRingSpliceMatch(buffer.data(), n, base, older, minOffset,
                                                                         maxOffset, matchLength);
                INFO("rate " << sampleRate << " fill " << static_cast<int>(fill) << " trial " << trial << " base "
                             << base << " older " << older << " offsets [" << minOffset << ", " << maxOffset
                             << "] match " << matchLength);
                REQUIRE(got.found == want.found);
                if (!want.found) {
                    ++silent;
                    continue;
                }
                CHECK(got.offset == want.offset);
                CHECK(got.score == want.score);  // bit for bit: the same operations in the same order
                ++compared;
                for (std::size_t offset = minOffset; offset <= maxOffset; ++offset) {
                    const std::size_t end = older ? (base + n - offset) % n : (base + offset) % n;
                    if (end + 1 < matchLength) {  // this candidate's window crosses index 0
                        ++wrapped;
                        break;
                    }
                }
            }
        }
    }
    INFO("compared " << compared << ", wrapping " << wrapped << ", silent " << silent);
    CHECK(compared > 400);
    CHECK(wrapped > 50);  // windows that cross index 0 take the per-candidate path
    CHECK(silent >= 120);
}

TEST_CASE("findRingSpliceMatch on a periodic buffer lands a period away, as the frozen search does",
          "[core][ring_splice]") {
    // A 100-sample period, offsets [150, 400], older: the best match is two periods back (offset 200), score 1.
    const std::size_t n = 1323 * 4 + 64;
    std::vector<float> buffer(n);
    for (std::size_t i = 0; i < n; ++i) {
        buffer[i] = static_cast<float>(0.5 * std::sin(6.283185307179586 * static_cast<double>(i) / 100.0));
    }
    const auto got = Krate::DSP::detail::findRingSpliceMatch(buffer.data(), n, 3000, true, 150, 400, 200);
    const Frozen want = frozenSearch(buffer, 3000, true, 150, 400, 200);
    REQUIRE(got.found);
    CHECK(got.offset == want.offset);
    CHECK(got.score == want.score);
    CHECK(got.offset % 100 == 0);
    CHECK(got.score > 0.999);
}
