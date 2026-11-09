// ==============================================================================
// Layer 1: Primitive Tests - BiquadCascade block paths, IEEE TU
// ==============================================================================
// Disrumpo audit T5.09 (D-130, follow-up 1): BiquadCascade::processBlock runs
// sample-major with the state in locals, and with four stages on x86 as a
// skewed four-lane SSE2 pipeline; processBlockPair runs two cascades in one
// loop, and the stereo IIR Oversampler uses it. All of them must give the
// stage-by-stage Biquad::process output bit for bit, including a non-finite
// stage input (that stage resets and yields 0) and every block length around
// the pipeline's fill / drain.
//
// Built only into dsp_ieee_tests (-fno-fast-math): it asserts bit-exactness
// between separately written computations and injects NaN/+-Inf built from bit
// patterns through a volatile.
// ==============================================================================

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/primitives/biquad.h>
#include <krate/dsp/primitives/oversampler.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

using namespace Krate::DSP;

namespace {

float bcbFromBits(std::uint32_t bits) noexcept {
    volatile std::uint32_t sink = bits;
    return std::bit_cast<float>(static_cast<std::uint32_t>(sink));
}

/// Deterministic noise in [-1, 1].
std::vector<float> bcbNoise(size_t n, std::uint32_t seed) {
    std::vector<float> x(n);
    std::uint32_t s = seed;
    for (auto& v : x) {
        s = s * 1664525u + 1013904223u;
        v = static_cast<float>(s >> 8) / 8388608.0f - 1.0f;
    }
    return x;
}

/// The stage-by-stage reference: each stage's Biquad::process over the whole buffer.
template <size_t N>
void bcbReference(BiquadCascade<N>& cascade, float* buffer, size_t n) {
    for (size_t s = 0; s < N; ++s) {
        for (size_t i = 0; i < n; ++i) buffer[i] = cascade.stage(s).process(buffer[i]);
    }
}

bool bcbSameBits(const std::vector<float>& a, const std::vector<float>& b, size_t& firstDiff) {
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::bit_cast<std::uint32_t>(a[i]) != std::bit_cast<std::uint32_t>(b[i])) {
            firstDiff = i;
            return false;
        }
    }
    return true;
}

template <size_t N>
void bcbCheckBlocks(const std::vector<size_t>& blockLengths, const std::vector<std::pair<size_t, std::uint32_t>>& bad) {
    BiquadCascade<N> fast;
    fast.setButterworth(FilterType::Lowpass, 19845.0f, 88200.0f);  // the oversampler's stage
    BiquadCascade<N> ref = fast;
    size_t total = 0;
    for (size_t len : blockLengths) total += len;
    auto in = bcbNoise(total, 12345u + static_cast<std::uint32_t>(N));
    for (const auto& [pos, bits] : bad) {
        if (pos < total) in[pos] = bcbFromBits(bits);
    }
    std::vector<float> outFast = in;
    std::vector<float> outRef = in;
    size_t start = 0;
    for (size_t len : blockLengths) {
        fast.processBlock(outFast.data() + start, len);
        bcbReference(ref, outRef.data() + start, len);
        start += len;
    }
    size_t firstDiff = 0;
    INFO("stages " << N << ", " << blockLengths.size() << " blocks, " << total << " samples");
    const bool same = bcbSameBits(outFast, outRef, firstDiff);
    INFO("first differing sample " << firstDiff);
    REQUIRE(same);
    for (size_t s = 0; s < N; ++s) {
        REQUIRE(std::bit_cast<std::uint32_t>(fast.stage(s).getZ1()) == std::bit_cast<std::uint32_t>(ref.stage(s).getZ1()));
        REQUIRE(std::bit_cast<std::uint32_t>(fast.stage(s).getZ2()) == std::bit_cast<std::uint32_t>(ref.stage(s).getZ2()));
    }
}

const std::vector<size_t> kBcbLengths{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 64, 1027, 3, 4, 512, 1};

} // namespace

TEST_CASE("BiquadCascade::processBlock equals stage-by-stage Biquad::process bit for bit",
          "[biquad][cascade][T5.09]") {
    bcbCheckBlocks<1>(kBcbLengths, {});
    bcbCheckBlocks<2>(kBcbLengths, {});
    bcbCheckBlocks<3>(kBcbLengths, {});
    bcbCheckBlocks<4>(kBcbLengths, {});
    bcbCheckBlocks<8>(kBcbLengths, {});
}

TEST_CASE("BiquadCascade::processBlock resets a stage on a non-finite input like Biquad::process",
          "[biquad][cascade][nonfinite][T5.09]") {
    // Positions inside the fill (0-2), the steady part, a block's last three samples (the
    // drain) and right after a block boundary; NaN, +Inf, -Inf, and a finite value large
    // enough to overflow a stage to Inf (a non-finite input to the NEXT stage)
    const std::vector<std::pair<size_t, std::uint32_t>> bad{
        {0, 0x7FC00000u}, {2, 0x7F800000u}, {40, 0xFF800000u}, {41, 0x7FC00000u},
        {100, 0x7F7FFFFFu}, {1134, 0x7FC00000u}, {1136, 0x7F800000u}, {1654, 0xFF7FFFFFu}};
    bcbCheckBlocks<2>(kBcbLengths, bad);
    bcbCheckBlocks<4>(kBcbLengths, bad);
}

TEST_CASE("BiquadCascade::processBlockPair equals two processBlock calls bit for bit",
          "[biquad][cascade][T5.09]") {
    BiquadCascade<4> a;
    a.setButterworth(FilterType::Lowpass, 19845.0f, 176400.0f);
    BiquadCascade<4> b = a;
    BiquadCascade<4> refA = a;
    BiquadCascade<4> refB = a;
    auto inA = bcbNoise(2200, 7u);
    auto inB = bcbNoise(2200, 8u);
    inB[333] = bcbFromBits(0x7FC00000u);
    std::vector<float> pa = inA;
    std::vector<float> pb = inB;
    std::vector<float> ra = inA;
    std::vector<float> rb = inB;
    size_t start = 0;
    for (size_t len : {size_t{3}, size_t{1024}, size_t{5}, size_t{1168}}) {
        BiquadCascade<4>::processBlockPair(a, pa.data() + start, b, pb.data() + start, len);
        refA.processBlock(ra.data() + start, len);
        refB.processBlock(rb.data() + start, len);
        start += len;
    }
    size_t firstDiff = 0;
    REQUIRE(bcbSameBits(pa, ra, firstDiff));
    REQUIRE(bcbSameBits(pb, rb, firstDiff));
}

TEST_CASE("Stereo IIR Oversampler equals two mono oversamplers bit for bit", "[oversampler][T5.09]") {
    const auto check = [](auto& stereo, auto& monoL, auto& monoR) {
        constexpr size_t kBlock = 512;
        stereo.prepare(48000.0, kBlock);
        monoL.prepare(48000.0, kBlock);
        monoR.prepare(48000.0, kBlock);
        REQUIRE_FALSE(stereo.isUsingFir());
        const auto shaper = [](float* x, size_t n) {
            for (size_t i = 0; i < n; ++i) x[i] = std::tanh(3.0f * x[i]);
        };
        auto inL = bcbNoise(kBlock * 6, 21u);
        auto inR = bcbNoise(kBlock * 6, 22u);
        std::vector<float> sL = inL;
        std::vector<float> sR = inR;
        std::vector<float> mL = inL;
        std::vector<float> mR = inR;
        for (size_t blk = 0; blk < 6; ++blk) {
            const size_t len = (blk == 3) ? 7 : kBlock;  // one short block
            float* l = sL.data() + blk * kBlock;
            float* r = sR.data() + blk * kBlock;
            stereo.process(l, r, len, [&shaper](float* ol, float* orr, size_t n) {
                shaper(ol, n);
                shaper(orr, n);
            });
            monoL.process(mL.data() + blk * kBlock, len, [&shaper](float* o, size_t n) { shaper(o, n); });
            monoR.process(mR.data() + blk * kBlock, len, [&shaper](float* o, size_t n) { shaper(o, n); });
        }
        size_t firstDiff = 0;
        REQUIRE(bcbSameBits(sL, mL, firstDiff));
        REQUIRE(bcbSameBits(sR, mR, firstDiff));
    };
    // Heap: an oversampler holds its FIR filter banks inline
    check(*std::make_unique<Oversampler<2, 2>>(), *std::make_unique<Oversampler<2, 1>>(),
          *std::make_unique<Oversampler<2, 1>>());
    check(*std::make_unique<Oversampler<4, 2>>(), *std::make_unique<Oversampler<4, 1>>(),
          *std::make_unique<Oversampler<4, 1>>());
}
