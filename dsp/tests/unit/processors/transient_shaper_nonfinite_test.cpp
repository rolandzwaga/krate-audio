// ==============================================================================
// Layer 2: Processor Tests - TransientShaper, non-finite TU
//                              (Profundum Phase 6, specs/profundum-phase6-mass-dynamics)
// ==============================================================================
// Reference: specs/profundum-phase6-mass-dynamics/spec.md (FR-007, FR-009; SC-019 shaper arm)
//            specs/profundum-phase6-mass-dynamics/tasks.md (T002 wires this TU, T008 fills it)
//
// SCOPE (tags [processors][profundum][transient-shaper][nonfinite]):
//   TransientShaper_NonFinite   SC-019 shaper arm, FR-007 (input sanitise), FR-009 (setters)
//
// Listed in the -fno-fast-math block of dsp/tests/CMakeLists.txt: it injects NaN / +-Inf built
// from bit patterns. Output finiteness is read only through TransientShaperTest::isFiniteBits.
//   Layer rule: no systems/ include, directly or through a helper.
// ==============================================================================

#include "transient_shaper_test_helpers.h"

#include <krate/dsp/processors/transient_shaper.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <vector>

using namespace Krate::DSP;
namespace tst = Krate::DSP::TransientShaperTest;

namespace {

constexpr double kTsnfFs = 48000.0;
constexpr std::size_t kTsnfHostBlock = 512;
constexpr std::size_t kTsnfBurstLen = 16;
constexpr std::size_t kTsnfStateCheckAfter = 512;  // "state finite within 1 block after the burst"
constexpr std::size_t kTsnfNumBursts = 6;

constexpr std::array<std::uint32_t, 3> kTsnfBadBits{0x7FC00000u, 0x7F800000u, 0xFF800000u};  // NaN, +Inf, -Inf
constexpr std::array<const char*, 3> kTsnfBadNames{"NaN", "+Inf", "-Inf"};

float tsnfFromBits(std::uint32_t bits) noexcept { return std::bit_cast<float>(bits); }

struct TsnfBurst {
    std::size_t start;
    std::size_t patternIndex;  // into kTsnfBadBits
    bool bothChannels;
};

/// Renders L / R in place in host blocks of at most kTsnfHostBlock; a block also ends at every
/// checkpoint (sorted, strictly increasing), where stateFinite() is recorded into `stateOk`.
void tsnfRender(TransientShaper& sh, std::vector<float>& L, std::vector<float>& R,
                const std::vector<std::size_t>& checkpoints, std::vector<bool>& stateOk) {
    REQUIRE(L.size() == R.size());
    stateOk.assign(checkpoints.size(), false);
    const std::size_t total = L.size();
    std::size_t pos = 0;
    std::size_t ci = 0;
    while (pos < total) {
        std::size_t end = std::min(total, pos + kTsnfHostBlock);
        if (ci < checkpoints.size())
            end = std::min(end, checkpoints[ci]);
        sh.processBlock(L.data() + pos, R.data() + pos, end - pos);
        pos = end;
        while (ci < checkpoints.size() && checkpoints[ci] == pos) {
            stateOk[ci] = sh.stateFinite();
            ++ci;
        }
    }
    REQUIRE(ci == checkpoints.size());
}

}  // namespace

// SC-019 shaper arm (FR-007, FR-009): 16-sample NaN / +Inf / -Inf bursts on L only, then on both
// channels, 0.5 s apart after 1 s steady; every output finite, the sanitised samples exactly 0.0f,
// state finite 512 samples after each burst, L's one-period level 100 ms after each burst within
// 0.1 dB of the pre-burst window; non-finite setters keep the previous value.
TEST_CASE("TransientShaper_NonFinite", "[processors][profundum][transient-shaper][nonfinite]") {
    const double fs = kTsnfFs;
    const double f0 = tst::kE1Hz;

    SECTION("input bursts") {
        // Bursts k = 0..5 at 1.0 + 0.5·k s: NaN, +Inf, -Inf on L only (k < 3), then on both channels.
        std::vector<TsnfBurst> bursts;
        for (std::size_t k = 0; k < kTsnfNumBursts; ++k)
            bursts.push_back(TsnfBurst{.start = tst::secondsToSamples(fs, 1.0 + 0.5 * static_cast<double>(k)),
                                       .patternIndex = k % 3,
                                       .bothChannels = k >= 3});
        const double seconds = 1.0 + 0.5 * static_cast<double>(kTsnfNumBursts);  // 0.5 s after the last burst

        // kSawAnchor E1 at -12 dBFS RMS, L == R (tasks.md rule 9).
        const std::vector<float> x = tst::steadyTone(tst::Tone::Saw, f0, fs, seconds);
        std::vector<float> inL = x;
        std::vector<float> inR = x;
        for (const TsnfBurst& b : bursts) {
            REQUIRE(b.start + kTsnfBurstLen + kTsnfStateCheckAfter <= x.size());
            const float bad = tsnfFromBits(kTsnfBadBits[b.patternIndex]);
            for (std::size_t i = 0; i < kTsnfBurstLen; ++i) {
                inL[b.start + i] = bad;
                if (b.bothChannels)
                    inR[b.start + i] = bad;
            }
        }

        TransientShaper sh;
        sh.setAttackGainDb(12.0f);
        sh.setSustainGainDb(-6.0f);
        sh.prepare(fs, kTsnfHostBlock);

        std::vector<std::size_t> checkpoints;
        for (const TsnfBurst& b : bursts)
            checkpoints.push_back(b.start + kTsnfBurstLen + kTsnfStateCheckAfter);

        std::vector<float> outL = inL;
        std::vector<float> outR = inR;
        std::vector<bool> stateOk;
        tsnfRender(sh, outL, outR, checkpoints, stateOk);

        // Every output sample finite (bit pattern); each sample whose input was non-finite is exactly 0.0f.
        std::size_t nonFiniteOut = 0;
        std::size_t sanitisedNotZero = 0;
        std::size_t sanitisedCount = 0;
        for (std::size_t n = 0; n < outL.size(); ++n) {
            if (!tst::isFiniteBits(outL[n]))
                ++nonFiniteOut;
            if (!tst::isFiniteBits(outR[n]))
                ++nonFiniteOut;
            if (!tst::isFiniteBits(inL[n])) {
                ++sanitisedCount;
                if (outL[n] != 0.0f)
                    ++sanitisedNotZero;
            }
            if (!tst::isFiniteBits(inR[n])) {
                ++sanitisedCount;
                if (outR[n] != 0.0f)
                    ++sanitisedNotZero;
            }
        }
        std::printf("TransientShaper_NonFinite: non-finite outputs %zu, sanitised input samples %zu, of which "
                    "non-zero at the output %zu\n",
                    nonFiniteOut, sanitisedCount, sanitisedNotZero);
        CHECK(sanitisedCount == kTsnfBurstLen * 9);  // 3 L-only bursts + 3 two-channel bursts
        CHECK(nonFiniteOut == 0);
        CHECK(sanitisedNotZero == 0);

        // One E1 period of L, 100 ms after each burst ends, vs the same-length window ending at the
        // burst onset.
        const auto periodLen = static_cast<std::size_t>(std::ceil(fs / f0));
        const std::size_t after = tst::secondsToSamples(fs, 0.1);
        for (std::size_t k = 0; k < bursts.size(); ++k) {
            const TsnfBurst& b = bursts[k];
            INFO("burst " << k << " " << kTsnfBadNames[b.patternIndex] << (b.bothChannels ? " L+R" : " L only"));
            const bool stateFiniteAfter = stateOk[k];
            CHECK(stateFiniteAfter);

            REQUIRE(b.start >= periodLen);
            const std::size_t postStart = b.start + kTsnfBurstLen + after;
            REQUIRE(postStart + periodLen <= outL.size());
            const double preDb = tst::levelDbPeriods(outL.data() + (b.start - periodLen), periodLen, fs, f0);
            const double postDb = tst::levelDbPeriods(outL.data() + postStart, periodLen, fs, f0);
            const double delta = postDb - preDb;
            std::printf("TransientShaper_NonFinite: burst %zu %s %s pre %.6f dB post %.6f dB delta %.6f dB "
                        "stateFinite %d\n",
                        k, kTsnfBadNames[b.patternIndex], b.bothChannels ? "L+R" : "L", preDb, postDb, delta,
                        stateFiniteAfter ? 1 : 0);
            INFO("pre " << preDb << " dB, post " << postDb << " dB");
            CHECK(std::abs(delta) <= 0.1);
        }
    }

    SECTION("non-finite setters keep the previous value") {
        for (const bool prepared : {false, true}) {
            for (std::size_t p = 0; p < kTsnfBadBits.size(); ++p) {
                INFO((prepared ? "after prepare, " : "before prepare, ") << kTsnfBadNames[p]);
                TransientShaper sh;
                sh.setAttackGainDb(12.0f);
                sh.setSustainGainDb(-6.0f);
                sh.setTransientDurationMs(100.0f);
                if (prepared)
                    sh.prepare(fs, kTsnfHostBlock);
                const float a0 = sh.attackGainDb();
                const float s0 = sh.sustainGainDb();
                const float d0 = sh.transientDurationMs();
                CHECK(a0 == 12.0f);
                CHECK(s0 == -6.0f);
                CHECK(d0 == 100.0f);

                const float bad = tsnfFromBits(kTsnfBadBits[p]);
                sh.setAttackGainDb(bad);
                sh.setSustainGainDb(bad);
                sh.setTransientDurationMs(bad);
                CHECK(std::bit_cast<std::uint32_t>(sh.attackGainDb()) == std::bit_cast<std::uint32_t>(a0));
                CHECK(std::bit_cast<std::uint32_t>(sh.sustainGainDb()) == std::bit_cast<std::uint32_t>(s0));
                CHECK(std::bit_cast<std::uint32_t>(sh.transientDurationMs()) == std::bit_cast<std::uint32_t>(d0));

                if (!prepared)
                    sh.prepare(fs, kTsnfHostBlock);
                // The shaper still renders finite output after the non-finite setter calls.
                std::vector<float> L = tst::steadyTone(tst::Tone::Saw, f0, fs, 0.25);
                std::vector<float> R = L;
                for (std::size_t pos = 0; pos < L.size(); pos += kTsnfHostBlock) {
                    const std::size_t n = std::min(kTsnfHostBlock, L.size() - pos);
                    sh.processBlock(L.data() + pos, R.data() + pos, n);
                }
                std::size_t badOut = 0;
                for (std::size_t n = 0; n < L.size(); ++n)
                    badOut += (tst::isFiniteBits(L[n]) && tst::isFiniteBits(R[n])) ? 0u : 1u;
                CHECK(badOut == 0);
                CHECK(sh.stateFinite());
            }
        }
    }
}
