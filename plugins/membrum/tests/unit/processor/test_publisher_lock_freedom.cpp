// ==============================================================================
// Publisher lock-freedom compile-time check
// ==============================================================================
//
// PadGlowPublisher carries
//     static_assert(std::atomic<std::uint32_t>::is_always_lock_free, ...);
// inside its constructor, so merely including the header and default-
// constructing an instance is sufficient to prove the compile-time assertion
// fires on any platform that is missing lock-free 32-bit atomics.
// ==============================================================================

#include "dsp/pad_glow_publisher.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>

// Belt-and-braces: redeclare the invariant at test-TU scope so a caller can
// see the failure in grep output even before linking the publisher header.
static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
              "32-bit atomic must be always lock-free for publisher contracts");

TEST_CASE("PadGlowPublisher is lock-free",
          "[publisher_lock_free]")
{
    Membrum::PadGlowPublisher glow;

    std::atomic<std::uint32_t> probe{0u};
    REQUIRE(probe.is_lock_free());

    glow.reset();
    REQUIRE(glow.readPadBucket(0) == 0);
}
