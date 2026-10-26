// ==============================================================================
// Layer 2: Processor Tests - Rectifier, non-finite TU
//                          (Profundum Phase 5, specs/profundum-phase5-shape-character)
// ==============================================================================
// Reference: specs/profundum-phase5-shape-character/plan.md (S4.3, S8)
//            specs/profundum-phase5-shape-character/tasks.md (T002 wires this TU,
//                                                             T007 fills it)
//
// SCOPE OF THIS TU (tags [processors][profundum][rectifier][nonfinite]): the
//   SC-017 Rectifier arm (FR-066) and the non-finite setter rule. Listed in the
//   -fno-fast-math block of dsp/tests/CMakeLists.txt: it injects NaN/±Inf built
//   from bit patterns. Finiteness is read only through detail::isFinite.
//   Layer rule: no systems/ include, directly or through a helper.
// ==============================================================================

#include "harmonic_fold_test_helpers.h"
#include "shape_character_metric_helpers.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/processors/rectifier.h>
#include <krate/dsp/processors/spectral_shape_recipe.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace Krate::DSP;
namespace hft = Krate::DSP::HarmonicFoldTest;
namespace sct = Krate::DSP::ShapeCharacterTest;

namespace {

constexpr std::uint32_t kRnfQuietNanBits = 0x7FC00000u;
constexpr std::uint32_t kRnfPosInfBits = 0x7F800000u;
constexpr std::uint32_t kRnfNegInfBits = 0xFF800000u;

float rnfFromBits(std::uint32_t bits) noexcept { return std::bit_cast<float>(bits); }

double rnfMidiHz(int note) { return 440.0 * std::pow(2.0, (static_cast<double>(note) - 69.0) / 12.0); }

/// Stereo in-place render in `block`-sized blocks; a block also ends at every index in `splits`
/// (sorted); `onBlock(pos)` runs before each block.
template <class OnBlock>
void rnfRenderStereo(Rectifier& r, std::vector<float>& L, std::vector<float>& R, std::size_t block,
                     const std::vector<std::size_t>& splits, const OnBlock& onBlock) {
    REQUIRE(block > 0);
    REQUIRE(L.size() == R.size());
    const std::size_t total = L.size();
    std::size_t pos = 0;
    std::size_t si = 0;
    while (pos < total) {
        while (si < splits.size() && splits[si] <= pos)
            ++si;
        std::size_t n = std::min(block, total - pos);
        if (si < splits.size())
            n = std::min(n, splits[si] - pos);
        onBlock(pos);
        r.processBlock(L.data() + pos, R.data() + pos, n);
        pos += n;
    }
}

struct RnfFrameDescriptors {
    double L;      ///< level, dBFS (sct::levelDb)
    double Elow;   ///< Σ_{2..8} w / Σ w, dB
    double Ehigh;  ///< Σ_{n≥9} w / Σ w, dB
    double C;      ///< centroid, octaves above f0 (sct::centroidOct)
};

/// Descriptors of the frame y[0..n): w_n from the first kLowFrequencyFftSize samples
/// (HarmonicFoldTest::renderedHarmonicPowers), L and C on the same frame through the T006 helpers.
RnfFrameDescriptors rnfDescribe(const float* y, std::size_t n, double fs, double f0) {
    REQUIRE(n >= TestUtils::kLowFrequencyFftSize);
    const int nHarm = static_cast<int>(std::floor(fs * 0.5 / f0));
    const std::vector<double> w = hft::renderedHarmonicPowers(y, fs, f0, nHarm);
    double total = 0.0;
    double low = 0.0;
    double high = 0.0;
    for (std::size_t i = 0; i < w.size(); ++i) {
        const std::size_t h = i + 1;
        total += w[i];
        if (h >= 2 && h <= 8)
            low += w[i];
        else if (h >= 9)
            high += w[i];
    }
    REQUIRE(total > 0.0);
    return RnfFrameDescriptors{.L = sct::levelDb(y, n, fs, f0),
                               .Elow = hft::safeDb10(low / total),
                               .Ehigh = hft::safeDb10(high / total),
                               .C = sct::centroidOct(y, n, fs, f0)};
}

}  // namespace

// =============================================================================
// SC-017 Rectifier arm (FR-066): a NaN / ±Inf burst is absorbed and the state recovers
// =============================================================================

TEST_CASE("Rectifier_NonFiniteRecovery", "[processors][profundum][rectifier][nonfinite]") {
    constexpr double fs = 48000.0;
    const double f0 = rnfMidiHz(28);  // E1
    constexpr std::size_t kBurstStart = 48000;  // 1.0 s
    constexpr std::size_t kBurstLen = 16;
    constexpr std::size_t kBurstEnd = kBurstStart + kBurstLen;
    constexpr std::size_t kRecoverySamples = 512;
    const auto frameStart = kBurstEnd + static_cast<std::size_t>(0.1 * fs);  // 100 ms after the burst end
    constexpr std::size_t kFrame = TestUtils::kLowFrequencyFftSize;
    const double seconds = static_cast<double>(frameStart + kFrame) / fs + 0.01;

    const std::vector<float> in = hft::recipeAdditive(SpectralShapeRecipe::kSawAnchor, f0, fs, seconds);
    REQUIRE(in.size() >= frameStart + kFrame);

    // Burst-free reference.
    Rectifier ref;
    ref.setBlend(0.5f);
    ref.prepare(fs, 512);
    std::vector<float> refL = in;
    std::vector<float> refR = in;
    rnfRenderStereo(ref, refL, refR, 512, {}, [](std::size_t) {});

    // Burst render: NaN, +Inf, -Inf cycling over 16 samples on both channels.
    std::vector<float> L = in;
    std::vector<float> R = in;
    const std::array<float, 3> burst{rnfFromBits(kRnfQuietNanBits), rnfFromBits(kRnfPosInfBits),
                                     rnfFromBits(kRnfNegInfBits)};
    for (std::size_t k = 0; k < kBurstLen; ++k) {
        L[kBurstStart + k] = burst[k % 3];
        R[kBurstStart + k] = burst[(k + 1) % 3];
    }
    Rectifier r;
    r.setBlend(0.5f);
    r.prepare(fs, 512);
    bool finiteAfterBurst = false;
    bool checked = false;
    rnfRenderStereo(r, L, R, 512, {kBurstStart, kBurstEnd, kBurstEnd + kRecoverySamples}, [&](std::size_t pos) {
        if (pos == kBurstEnd + kRecoverySamples) {
            finiteAfterBurst = r.stateFinite();
            checked = true;
        }
    });
    REQUIRE(checked);
    CHECK(finiteAfterBurst);
    CHECK(r.stateFinite());

    std::size_t nonFinite = 0;
    for (std::size_t k = 0; k < L.size(); ++k) {
        if (!detail::isFinite(L[k]) || !detail::isFinite(R[k]))
            ++nonFinite;
    }
    INFO("non-finite output samples: " << nonFinite);
    CHECK(nonFinite == 0u);

    const RnfFrameDescriptors dBurst = rnfDescribe(L.data() + frameStart, kFrame, fs, f0);
    const RnfFrameDescriptors dRef = rnfDescribe(refL.data() + frameStart, kFrame, fs, f0);
    INFO("burst: L " << dBurst.L << " dB, Elow " << dBurst.Elow << " dB, Ehigh " << dBurst.Ehigh << " dB, C "
                     << dBurst.C << " oct; reference: L " << dRef.L << ", Elow " << dRef.Elow << ", Ehigh "
                     << dRef.Ehigh << ", C " << dRef.C);
    CHECK(std::abs(dBurst.L - dRef.L) <= 0.1);
    CHECK(std::abs(dBurst.Elow - dRef.Elow) <= 0.1);
    CHECK(std::abs(dBurst.Ehigh - dRef.Ehigh) <= 0.1);
    CHECK(std::abs(dBurst.C - dRef.C) <= 0.01);
}

TEST_CASE("Rectifier_NonFiniteSetter", "[processors][profundum][rectifier][nonfinite]") {
    constexpr double fs = 48000.0;
    const double f0 = rnfMidiHz(28);  // E1
    const std::vector<float> in = hft::recipeAdditive(SpectralShapeRecipe::kSawAnchor, f0, fs, 1.0);
    constexpr std::size_t kNan = 20000;
    constexpr std::size_t kPosInf = 24000;
    constexpr std::size_t kNegInf = 28000;

    Rectifier ref;
    ref.setBlend(0.5f);
    ref.prepare(fs, 512);
    std::vector<float> refL = in;
    std::vector<float> refR = in;
    rnfRenderStereo(ref, refL, refR, 512, {kNan, kPosInf, kNegInf}, [](std::size_t) {});

    Rectifier r;
    r.setBlend(0.5f);
    r.prepare(fs, 512);
    std::vector<float> L = in;
    std::vector<float> R = in;
    rnfRenderStereo(r, L, R, 512, {kNan, kPosInf, kNegInf}, [&](std::size_t pos) {
        if (pos == kNan)
            r.setBlend(rnfFromBits(kRnfQuietNanBits));
        else if (pos == kPosInf)
            r.setBlend(rnfFromBits(kRnfPosInfBits));
        else if (pos == kNegInf)
            r.setBlend(rnfFromBits(kRnfNegInfBits));
    });
    CHECK(r.blend() == 0.5f);
    CHECK(std::memcmp(L.data(), refL.data(), L.size() * sizeof(float)) == 0);
    CHECK(std::memcmp(R.data(), refR.data(), R.size() * sizeof(float)) == 0);
}
