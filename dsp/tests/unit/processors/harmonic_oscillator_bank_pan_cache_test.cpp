// ==============================================================================
// HarmonicOscillatorBank::applyPanOffsets' last-evaluation cache against the frozen loop, bit for bit
// ==============================================================================
// Innexus calls applyPanOffsets per sample, per voice and per enabled modulator, with all-zero offsets whenever the
// modulator does not target Pan: 2 x 96 cos / sin per call was 73 % of the M6 benchmark (SC-008). The bank now reuses
// its last tables when the positions and offsets are bit for bit the last evaluation's. This TU holds the original loop
// and asserts, after every call of a long mixed sequence (spread changes, frame loads, zero / constant / moving /
// clamping offsets, NaN offsets), that the gains in force are the frozen loop's for the bank's positions and the
// offsets, bit for bit; and that repeated positions really do skip the trig. A bit-identity claim between two
// separately compiled computations: it lives in dsp_ieee_tests.

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/processors/harmonic_oscillator_bank.h>
#include <krate/dsp/processors/harmonic_types.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <random>

using Krate::DSP::HarmonicFrame;
using Krate::DSP::HarmonicOscillatorBank;
using Krate::DSP::kMaxPartials;

namespace {

using Table = std::array<float, kMaxPartials>;

// HarmonicOscillatorBank::applyPanOffsets as of bdae97e5, verbatim but for writing into the test's tables.
void frozenApplyPanOffsets(const Table& panPosition_, const Table& offsets, Table& panLeft_, Table& panRight_) {
    constexpr float kQuarterPi = Krate::DSP::kPi / 4.0f;

    for (size_t i = 0; i < kMaxPartials; ++i)
    {
        float newPos = std::clamp(panPosition_[i] + offsets[i], -1.0f, 1.0f);
        float angle = kQuarterPi + newPos * kQuarterPi;
        panLeft_[i] = std::cos(angle);
        panRight_[i] = std::sin(angle);
    }
}

/// Bit for bit, except that two NaNs match whatever their payloads (cos / sin of a NaN position).
bool bitEqual(const Table& a, const Table& b) {
    for (size_t i = 0; i < kMaxPartials; ++i) {
        if (Krate::DSP::detail::isNaN(a[i]) && Krate::DSP::detail::isNaN(b[i])) continue;
        if (std::bit_cast<std::uint32_t>(a[i]) != std::bit_cast<std::uint32_t>(b[i])) return false;
    }
    return true;
}

HarmonicFrame makeFrame(int numPartials) {
    HarmonicFrame frame;
    frame.numPartials = numPartials;
    for (int n = 1; n <= numPartials; ++n) {
        auto& p = frame.partials[static_cast<size_t>(n - 1)];
        p.harmonicIndex = n;
        p.relativeFrequency = static_cast<float>(n);
        p.amplitude = 1.0f / static_cast<float>(n);
    }
    return frame;
}

}  // namespace

TEST_CASE("applyPanOffsets' cache gives the frozen loop's gains after every call, bit for bit",
          "[processors][harmonic_bank][pan_cache]") {
    HarmonicOscillatorBank bank;
    bank.prepare(44100.0);
    bank.loadFrame(makeFrame(24), 110.0f, true);

    std::mt19937 rng(0xC0FFEEu);
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    std::uniform_real_distribution<float> wide(-2.5f, 2.5f);  // pushes positions past the clamp
    Table moving{};
    Table constant{};
    for (auto& v : constant) v = 0.3f * u(rng);
    std::uint32_t nanBits = 0x7FC00000u;
    float nan = 0.0f;
    std::memcpy(&nan, &nanBits, sizeof(nan));

    int calls = 0;
    int checked = 0;
    Table modelPositions{};
    Table modelOffsets{};
    bool modelValid = false;
    int expectedRecomputes = 0;
    int expectedHits = 0;
    const int recomputesBefore = bank.panOffsetRecomputeCount();
    for (int step = 0; step < 4000; ++step) {
        // The Innexus per-sample order: the spread restore first, then each modulator's offsets.
        const float spread = (step / 500) % 2 == 0 ? 0.5f : 0.5f + 0.0001f * static_cast<float>(step % 500);
        bank.setStereoSpread(spread);
        if (step == 1700) {
            bank.loadFrame(makeFrame(40), 110.0f, true);  // new harmonic indices: new positions
        }
        if (step == 2900) {
            bank.reset();
            bank.loadFrame(makeFrame(12), 110.0f, true);
        }

        Table offsets{};
        switch ((step / 250) % 5) {
            case 0: break;                          // a modulator that does not target Pan: zeros
            case 1: offsets = constant; break;      // a held S&H / square value
            case 2:
                for (auto& v : moving) v = 0.5f * u(rng);
                offsets = moving;                    // a sine LFO: new every call
                break;
            case 3:
                for (auto& v : offsets) v = wide(rng);
                break;                               // clamped at +/-1
            default:
                offsets[static_cast<size_t>(step % 96)] = nan;  // a held NaN offset: cached NaN gains
                break;
        }
        for (int mod = 0; mod < 2; ++mod) {
            const Table positions = bank.panPositions();
            // The model of the cache: the trig runs unless the positions and the offsets are bit for bit the last
            // evaluated ones (object representations, as the bank compares them).
            // NOLINTBEGIN(bugprone-suspicious-memory-comparison)
            const bool same = modelValid && std::memcmp(positions.data(), modelPositions.data(), sizeof(Table)) == 0 &&
                              std::memcmp(offsets.data(), modelOffsets.data(), sizeof(Table)) == 0;
            // NOLINTEND(bugprone-suspicious-memory-comparison)
            if (!same) {
                ++expectedRecomputes;
                modelPositions = positions;
                modelOffsets = offsets;
                modelValid = true;
            } else {
                ++expectedHits;
            }
            bank.applyPanOffsets(offsets);
            ++calls;
            Table wantL{};
            Table wantR{};
            frozenApplyPanOffsets(positions, offsets, wantL, wantR);
            INFO("step " << step << " mod " << mod);
            REQUIRE(bitEqual(bank.panLeftGains(), wantL));
            REQUIRE(bitEqual(bank.panRightGains(), wantR));
            ++checked;
        }
    }
    const int recomputes = bank.panOffsetRecomputeCount() - recomputesBefore;
    INFO("calls " << calls << ", trig evaluations " << recomputes << ", model hits " << expectedHits);
    CHECK(checked == 8000);
    CHECK(recomputes == expectedRecomputes);
    CHECK(expectedHits > 2000);  // the schedule exercises the reuse path, not only the evaluation path
}

TEST_CASE("applyPanOffsets with zero offsets on a settled spread evaluates the trig once",
          "[processors][harmonic_bank][pan_cache]") {
    HarmonicOscillatorBank bank;
    bank.prepare(44100.0);
    bank.loadFrame(makeFrame(32), 110.0f, true);
    bank.setStereoSpread(0.7f);
    const Table zeros{};
    const int before = bank.panOffsetRecomputeCount();
    for (int sample = 0; sample < 512; ++sample) {
        bank.setStereoSpread(0.7f);
        bank.applyPanOffsets(zeros);
        bank.applyPanOffsets(zeros);
    }
    CHECK(bank.panOffsetRecomputeCount() - before == 1);
    Table wantL{};
    Table wantR{};
    frozenApplyPanOffsets(bank.panPositions(), zeros, wantL, wantR);
    CHECK(bitEqual(bank.panLeftGains(), wantL));
    CHECK(bitEqual(bank.panRightGains(), wantR));
}
