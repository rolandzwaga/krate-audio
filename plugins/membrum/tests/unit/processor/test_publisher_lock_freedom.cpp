// ==============================================================================
// Publisher lock-freedom compile-time check (Phase 6, T094)
// Spec: specs/141-membrum-phase6-ui/spec.md (FR-014, FR-052, R3)
// ==============================================================================
//
// Both PadGlowPublisher and MatrixActivityPublisher already carry
//     static_assert(std::atomic<std::uint32_t>::is_always_lock_free, ...);
// inside their constructors, so merely including the headers and default-
// constructing an instance is sufficient to prove the compile-time assertion
// fires on any platform that is missing lock-free 32-bit atomics.
//
// This test exists so a compiler-regression on lock-freedom is caught by a
// failing compile in the test target (which CI runs) rather than only during
// a plugin build.
// ==============================================================================

#include "dsp/pad_glow_publisher.h"
#include "dsp/matrix_activity_publisher.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>

// Belt-and-braces: redeclare the invariant at test-TU scope so a caller can
// see the failure in grep output even before linking the publisher headers.
static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
              "32-bit atomic must be always lock-free for publisher contracts");

TEST_CASE("PadGlowPublisher and MatrixActivityPublisher are lock-free (T094)",
          "[publisher_lock_free]")
{
    // Instantiation fires the in-constructor static_asserts.
    Membrum::PadGlowPublisher       glow;
    Membrum::MatrixActivityPublisher matrix;

    // Runtime confirmation: is_lock_free() can only return true because the
    // compile-time static_assert above already passed, but we include it for
    // symmetry with other lock-free tests in the repo.
    std::atomic<std::uint32_t> probe{0u};
    REQUIRE(probe.is_lock_free());

    // Touch the publishers so the compiler cannot discard them.
    glow.reset();
    matrix.reset();
    REQUIRE(glow.readPadBucket(0) == 0);
    REQUIRE(matrix.readSourceActivity(0) == 0u);
}
