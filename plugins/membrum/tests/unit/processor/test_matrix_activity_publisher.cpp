// ==============================================================================
// MatrixActivityPublisher unit tests (Phase 6, T012)
// Spec: specs/141-membrum-phase6-ui/spec.md (FR-052)
// Contract: specs/141-membrum-phase6-ui/contracts/matrix_activity_publisher.h
// ==============================================================================

#include "dsp/matrix_activity_publisher.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <cstdint>

using Membrum::MatrixActivityPublisher;

TEST_CASE("MatrixActivityPublisher: lock-free atomic guarantee",
          "[matrix_activity]")
{
    STATIC_REQUIRE(std::atomic<std::uint32_t>::is_always_lock_free);
}

TEST_CASE("MatrixActivityPublisher: footprint is 128 bytes (32 x uint32_t)",
          "[matrix_activity]")
{
    REQUIRE(sizeof(MatrixActivityPublisher) >= 128);
    REQUIRE(sizeof(MatrixActivityPublisher) <= 192);
}

TEST_CASE("MatrixActivityPublisher: publishSourceActivity round-trips",
          "[matrix_activity]")
{
    MatrixActivityPublisher pub;
    pub.publishSourceActivity(3, 0b101u);
    REQUIRE(pub.readSourceActivity(3) == 0b101u);
}

TEST_CASE("MatrixActivityPublisher: snapshot reads all 32 source masks",
          "[matrix_activity]")
{
    MatrixActivityPublisher pub;
    for (int s = 0; s < 32; ++s)
        pub.publishSourceActivity(s, static_cast<std::uint32_t>(s) << 1);

    std::array<std::uint32_t, 32> masks{};
    pub.snapshot(masks);
    for (int s = 0; s < 32; ++s)
        REQUIRE(masks[static_cast<std::size_t>(s)] ==
                (static_cast<std::uint32_t>(s) << 1));
}

TEST_CASE("MatrixActivityPublisher: reset zeroes all masks", "[matrix_activity]")
{
    MatrixActivityPublisher pub;
    for (int s = 0; s < 32; ++s)
        pub.publishSourceActivity(s, 0xFFFFFFFFu);

    pub.reset();

    std::array<std::uint32_t, 32> masks{};
    pub.snapshot(masks);
    for (auto m : masks)
        REQUIRE(m == 0u);
}

TEST_CASE("MatrixActivityPublisher: sources are independent", "[matrix_activity]")
{
    MatrixActivityPublisher pub;
    pub.publishSourceActivity(0,  0xAAu);
    pub.publishSourceActivity(15, 0u);
    pub.publishSourceActivity(31, 0x55u);

    REQUIRE(pub.readSourceActivity(0)  == 0xAAu);
    REQUIRE(pub.readSourceActivity(15) == 0u);
    REQUIRE(pub.readSourceActivity(31) == 0x55u);
}

TEST_CASE("MatrixActivityPublisher: out-of-range source is a no-op",
          "[matrix_activity]")
{
    MatrixActivityPublisher pub;
    pub.publishSourceActivity(-1, 0xFFu);
    pub.publishSourceActivity(32, 0xFFu);
    REQUIRE(pub.readSourceActivity(-1) == 0u);
    REQUIRE(pub.readSourceActivity(32) == 0u);
}
