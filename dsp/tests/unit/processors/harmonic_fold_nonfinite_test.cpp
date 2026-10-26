// ==============================================================================
// Layer 2: Processor Tests - HarmonicFold, non-finite TU
//                                    (Profundum Phase 2, specs/profundum-phase2-edge-fold)
// ==============================================================================
// Reference: specs/profundum-phase2-edge-fold/plan.md (S4.3, S8.1, S8.2)
//            specs/profundum-phase2-edge-fold/tasks.md (T006 wires this TU,
//                                                       T008 fills it)
//
// SCOPE OF THIS TU (plan S8.1): SC-016 Fold (HarmonicFold_NonFinite) and
//   HarmonicFold_TableLookupAtEndpoint (S4.3). Listed in the -fno-fast-math
//   block of dsp/tests/CMakeLists.txt: it injects NaN/Inf by bit pattern and
//   uses NaN table sentinels.
//
// T008: HarmonicFold_TableLookupAtEndpoint (S4.3) and
//   HarmonicFold_NonFiniteSetters (SC-016 setter part, FR-042, FR-043).
// T009: HarmonicFold_NonFinite (SC-016 sample part, FR-043).
// ==============================================================================

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/processors/harmonic_fold.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

using namespace Krate::DSP;

namespace {

float floatFromBits(std::uint32_t bits) noexcept { return std::bit_cast<float>(bits); }

constexpr std::uint32_t kQuietNanBits = 0x7FC00000u;
constexpr std::uint32_t kPosInfBits = 0x7F800000u;
constexpr std::uint32_t kNegInfBits = 0xFF800000u;
constexpr std::uint32_t kSmallestDenormalBits = 0x00000001u;

/// A table followed directly by a quiet-NaN sentinel: an unclamped read at a = 100 gives
/// 1·T[400] + 0·NaN = NaN, so the clamp is tested deterministically (plan S4.3).
struct SentinelTable {
    std::array<float, HarmonicFold::kTablePoints> t;
    float sentinel;
};
static_assert(offsetof(SentinelTable, sentinel) == sizeof(std::array<float, 401>));

}  // namespace

// =============================================================================
// S4.3 lookup at the endpoint
// =============================================================================

TEST_CASE("HarmonicFold_TableLookupAtEndpoint", "[processors][profundum][edge-fold]") {
    static_assert(detail::amountTableIndex(400.0f) == 399);
    static_assert(detail::amountTableIndex(0.0f) == 0);

    SentinelTable s{};
    for (std::size_t i = 0; i < s.t.size(); ++i)
        s.t[i] = static_cast<float>(i);
    s.sentinel = floatFromBits(kQuietNanBits);
    REQUIRE(detail::isNaN(s.sentinel));

    const float at100 = detail::lerpAmountTable(s.t, 100.0f);
    const float at9975 = detail::lerpAmountTable(s.t, 99.75f);
    const float at0 = detail::lerpAmountTable(s.t, 0.0f);

    REQUIRE(detail::isFinite(at100));
    REQUIRE(detail::isFinite(at9975));
    REQUIRE(detail::isFinite(at0));
    REQUIRE(at100 == 400.0f);
    REQUIRE(at9975 == 399.0f);
    REQUIRE(at0 == 0.0f);
}

// =============================================================================
// SC-016 setter part (FR-042, FR-043)
// =============================================================================

TEST_CASE("HarmonicFold_NonFiniteSetters", "[processors][profundum][edge-fold]") {
    const std::array<float, 3> nonFinite{floatFromBits(kQuietNanBits), floatFromBits(kPosInfBits),
                                         floatFromBits(kNegInfBits)};
    const std::array<float, 2> denormals{floatFromBits(kSmallestDenormalBits), 1e-40f};

    SECTION("setAmount, prepared") {
        HarmonicFold fold;
        fold.prepare(48000.0, 512);
        for (const float v : nonFinite) {
            fold.setAmount(42.0f);
            fold.setAmount(v);
            REQUIRE(fold.amount() == 42.0f);
        }
        for (const float v : denormals) {
            fold.setAmount(42.0f);
            fold.setAmount(v);
            REQUIRE(fold.amount() == 0.0f);
            REQUIRE(std::bit_cast<std::uint32_t>(fold.amount()) == 0u);
        }
    }

    SECTION("setFundamentalHz, prepared") {
        HarmonicFold fold;
        fold.prepare(48000.0, 512);
        for (const float v : nonFinite) {
            fold.setFundamentalHz(41.2034f);
            fold.setFundamentalHz(v);
            REQUIRE(fold.fundamentalHz() == 41.2034f);
        }
        for (const float v : denormals) {
            fold.setFundamentalHz(41.2034f);
            fold.setFundamentalHz(v);  // denormal -> 0 -> clamped to kMinFundamentalHz
            REQUIRE(fold.fundamentalHz() == 8.0f);
        }
        REQUIRE(detail::isFinite(fold.releaseSeconds()));
    }

    SECTION("setters before prepare store sanitised values (FR-042)") {
        HarmonicFold f;
        f.setAmount(250.0f);
        REQUIRE(f.amount() == 100.0f);
        f.setAmount(floatFromBits(kQuietNanBits));
        REQUIRE(f.amount() == 100.0f);
        f.setAmount(floatFromBits(kSmallestDenormalBits));
        REQUIRE(f.amount() == 0.0f);

        HarmonicFold g;
        g.setFundamentalHz(floatFromBits(kPosInfBits));
        REQUIRE(g.fundamentalHz() == HarmonicFold::kDefaultFundamentalHz);
        g.setFundamentalHz(1e-40f);
        REQUIRE(g.fundamentalHz() == 8.0f);
        g.setFundamentalHz(30000.0f);
        REQUIRE(g.fundamentalHz() == 20000.0f);
    }
}

// =============================================================================
// SC-016 sample part (FR-043): non-finite input samples are replaced by 0.0f
// =============================================================================

TEST_CASE("HarmonicFold_NonFinite", "[processors][profundum][edge-fold]") {
    constexpr double fs = 48000.0;
    constexpr double f0 = 41.20344461410875;  // E1
    constexpr double kTwoPiD = 6.283185307179586476925286766559;
    constexpr std::size_t kBlock = 4096;
    constexpr std::size_t kHistory = 24000;  // 0.5 s of shared history before the injected block

    // Calibration sine in double, cast once (as HarmonicFoldTest::calibrationSine).
    std::vector<float> sine(kHistory + kBlock, 0.0f);
    for (std::size_t n = 0; n < sine.size(); ++n)
        sine[n] = static_cast<float>(static_cast<double>(HarmonicFold::kCalibrationPeak)
                                     * std::sin(kTwoPiD * f0 * static_cast<double>(n) / fs));

    HarmonicFold fold;
    fold.setAmount(100.0f);
    fold.prepare(fs, 512);
    fold.setFundamentalHz(static_cast<float>(f0));

    // Shared history.
    for (std::size_t pos = 0; pos < kHistory; pos += 512) {
        std::vector<float> l(sine.begin() + static_cast<std::ptrdiff_t>(pos),
                             sine.begin() + static_cast<std::ptrdiff_t>(pos + 512));
        std::vector<float> r = l;
        fold.processBlock(l.data(), r.data(), l.size());
    }
    REQUIRE(fold.stateFinite());
    HarmonicFold twin = fold;

    const std::vector<float> block(sine.begin() + static_cast<std::ptrdiff_t>(kHistory), sine.end());
    std::vector<float> L = block;
    std::vector<float> R = block;
    std::vector<float> L0 = block;
    std::vector<float> R0 = block;
    const std::array<std::pair<std::size_t, std::uint32_t>, 3> leftHits{
        {{10, kQuietNanBits}, {500, kPosInfBits}, {2047, kNegInfBits}}};
    const std::array<std::pair<std::size_t, std::uint32_t>, 2> rightHits{{{11, kQuietNanBits}, {501, kPosInfBits}}};
    for (const auto& [pos, bits] : leftHits) {
        L[pos] = floatFromBits(bits);
        L0[pos] = 0.0f;
    }
    for (const auto& [pos, bits] : rightHits) {
        R[pos] = floatFromBits(bits);
        R0[pos] = 0.0f;
    }

    fold.processBlock(L.data(), R.data(), kBlock);
    twin.processBlock(L0.data(), R0.data(), kBlock);

    for (std::size_t k = 0; k < kBlock; ++k) {
        REQUIRE(detail::isFinite(L[k]));
        REQUIRE(detail::isFinite(R[k]));
    }
    REQUIRE(fold.stateFinite());
    const auto sameBits = [](float a, float b) {
        return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
    };
    REQUIRE(std::equal(L.begin(), L.end(), L0.begin(), sameBits));
    REQUIRE(std::equal(R.begin(), R.end(), R0.begin(), sameBits));
}
