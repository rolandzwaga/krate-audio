// ==============================================================================
// PadGlowPublisher unit tests (Phase 6, T011)
// Spec: specs/141-membrum-phase6-ui/spec.md (FR-014, FR-101, SC-005)
// Contract: specs/141-membrum-phase6-ui/contracts/pad_glow_publisher.h
// ==============================================================================

#include "dsp/pad_glow_publisher.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <cstdint>

using Membrum::PadGlowPublisher;

TEST_CASE("PadGlowPublisher: lock-free atomic guarantee", "[pad_glow_publisher]")
{
    STATIC_REQUIRE(std::atomic<std::uint32_t>::is_always_lock_free);
}

TEST_CASE("PadGlowPublisher: footprint is 128 bytes (32 x uint32_t)",
          "[pad_glow_publisher]")
{
    // 32 atomic words = 128 bytes; allow alignment padding up to one cache line.
    REQUIRE(sizeof(PadGlowPublisher) >= 128);
    REQUIRE(sizeof(PadGlowPublisher) <= 192);
}

TEST_CASE("PadGlowPublisher: amplitude 0.0 stores word 0", "[pad_glow_publisher]")
{
    PadGlowPublisher pub;
    pub.publish(0, 0.0f);
    REQUIRE(pub.readPadBucket(0) == 0);
}

TEST_CASE("PadGlowPublisher: amplitude 1.0 stores highest-bucket one-hot",
          "[pad_glow_publisher]")
{
    PadGlowPublisher pub;
    pub.publish(0, 1.0f);
    // Highest bucket index = 31 (kAmplitudeBuckets - 1).
    REQUIRE(pub.readPadBucket(0) == 31);
}

TEST_CASE("PadGlowPublisher: amplitude 0.5 maps to bucket ~16",
          "[pad_glow_publisher]")
{
    PadGlowPublisher pub;
    pub.publish(0, 0.5f);
    // 0.5 * 31 + 0.5 = 16.0  -> bucket 16
    const auto bucket = pub.readPadBucket(0);
    REQUIRE(bucket >= 15);
    REQUIRE(bucket <= 17);
}

TEST_CASE("PadGlowPublisher: snapshot reads all 32 words", "[pad_glow_publisher]")
{
    PadGlowPublisher pub;
    for (int i = 0; i < 32; ++i)
        pub.publish(i, static_cast<float>(i) / 31.0f);

    std::array<std::uint8_t, 32> buckets{};
    pub.snapshot(buckets);

    // Pad 0 -> bucket 0 (silent), pad 31 -> bucket 31.
    REQUIRE(buckets[0] == 0);
    REQUIRE(buckets[31] == 31);

    // Monotonically non-decreasing across pads.
    for (std::size_t i = 1; i < buckets.size(); ++i)
        REQUIRE(buckets[i] >= buckets[i - 1]);
}

TEST_CASE("PadGlowPublisher: reset zeroes all 32 words", "[pad_glow_publisher]")
{
    PadGlowPublisher pub;
    for (int i = 0; i < 32; ++i)
        pub.publish(i, 1.0f);

    pub.reset();

    std::array<std::uint8_t, 32> buckets{};
    pub.snapshot(buckets);
    for (auto b : buckets)
        REQUIRE(b == 0);
}

TEST_CASE("PadGlowPublisher: multi-pad publishes are independent",
          "[pad_glow_publisher]")
{
    PadGlowPublisher pub;
    pub.publish(0,  1.0f);
    pub.publish(15, 0.0f);
    pub.publish(31, 0.5f);

    REQUIRE(pub.readPadBucket(0)  == 31);
    REQUIRE(pub.readPadBucket(15) == 0);
    const auto mid = pub.readPadBucket(31);
    REQUIRE(mid >= 15);
    REQUIRE(mid <= 17);
}

TEST_CASE("PadGlowPublisher: out-of-range pad index is a no-op",
          "[pad_glow_publisher]")
{
    PadGlowPublisher pub;
    pub.publish(-1, 1.0f);
    pub.publish(32, 1.0f);
    pub.publish(99, 1.0f);
    REQUIRE(pub.readPadBucket(-1) == 0);
    REQUIRE(pub.readPadBucket(32) == 0);
}

TEST_CASE("PadGlowPublisher: amplitude clamped to [0, 1]", "[pad_glow_publisher]")
{
    PadGlowPublisher pub;
    pub.publish(0, -5.0f);
    REQUIRE(pub.readPadBucket(0) == 0);
    pub.publish(0, 7.0f);
    REQUIRE(pub.readPadBucket(0) == 31);
}
