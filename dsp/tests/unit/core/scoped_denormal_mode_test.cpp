// ==============================================================================
// ScopedDenormalMode tests
// ==============================================================================
// Regression coverage for the PR #262 review finding: FTZ/DAZ were being set in
// setupProcessing(), but MXCSR is per-thread state and setupProcessing() runs on
// the main thread while process() runs on the audio thread. The flags therefore
// never applied to the thread doing the DSP.
//
// These tests assert the two properties that fix depends on:
//   1. The guard actually flushes denormals *on the thread that constructs it*.
//   2. It restores the caller's mode on exit, so we don't clobber host FP state
//      on a shared render thread.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include <krate/dsp/core/scoped_denormal_mode.h>

#include <condition_variable>
#include <mutex>
#include <thread>

using Krate::DSP::ScopedDenormalMode;

#if KRATE_HAS_SSE_DENORMAL_CONTROL

namespace {

/// Smallest positive normal float is ~1.18e-38; half of it is denormal.
/// Written through a volatile sink so the compiler cannot constant-fold the
/// multiply at compile time (which would bypass MXCSR entirely).
[[nodiscard]] float denormalProduct() noexcept
{
    volatile float tiny = 1.0e-38f;
    volatile float scale = 0.01f;
    return tiny * scale;
}

/// True when the FPU is flushing denormal results to zero right now.
[[nodiscard]] bool denormalsAreFlushed() noexcept
{
    return denormalProduct() == 0.0f;
}

/// Saves and restores the ambient MXCSR denormal bits.
///
/// dsp_test_main.cpp enables FTZ/DAZ process-wide for the whole suite, so these
/// tests must put the mode back exactly as they found it -- otherwise they leak
/// a denormal-sensitive FP environment into whichever test Catch2's random
/// ordering happens to run next.
class AmbientDenormalModeRestorer {
public:
    AmbientDenormalModeRestorer() noexcept
        : flushZero_(_MM_GET_FLUSH_ZERO_MODE())
        , denormalsZero_(_MM_GET_DENORMALS_ZERO_MODE())
    {}

    ~AmbientDenormalModeRestorer() noexcept
    {
        _MM_SET_FLUSH_ZERO_MODE(flushZero_);
        _MM_SET_DENORMALS_ZERO_MODE(denormalsZero_);
    }

    AmbientDenormalModeRestorer(const AmbientDenormalModeRestorer&) = delete;
    AmbientDenormalModeRestorer& operator=(const AmbientDenormalModeRestorer&) = delete;

private:
    unsigned int flushZero_;
    unsigned int denormalsZero_;
};

} // namespace

TEST_CASE("ScopedDenormalMode flushes denormals inside the scope",
          "[dsp][core][denormal]")
{
    const AmbientDenormalModeRestorer restoreAmbient;

    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_OFF);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_OFF);

    // Baseline: without the guard the denormal survives.
    REQUIRE_FALSE(denormalsAreFlushed());

    {
        const ScopedDenormalMode guard;
        REQUIRE(denormalsAreFlushed());
    }

    // Restored: the caller's mode is back, denormals survive again.
    REQUIRE_FALSE(denormalsAreFlushed());
}

TEST_CASE("ScopedDenormalMode restores an already-enabled mode",
          "[dsp][core][denormal]")
{
    const AmbientDenormalModeRestorer restoreAmbient;

    // A host that already runs its render thread with FTZ on must get FTZ back,
    // not our idea of a default.
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);

    {
        const ScopedDenormalMode guard;
        REQUIRE(denormalsAreFlushed());
    }

    REQUIRE(_MM_GET_FLUSH_ZERO_MODE() == _MM_FLUSH_ZERO_ON);
    REQUIRE(_MM_GET_DENORMALS_ZERO_MODE() == _MM_DENORMALS_ZERO_ON);
}

TEST_CASE("Setting the denormal mode does not reach an already-running thread",
          "[dsp][core][denormal]")
{
    // The PR #262 bug, expressed as a test: a host calls setupProcessing() on
    // the main thread, while process() runs on an audio thread that ALREADY
    // EXISTS. Setting MXCSR on the former cannot affect the latter; only a
    // guard constructed on the audio thread itself works.
    //
    // The worker must be started BEFORE the main thread enables FTZ, and that
    // ordering is the whole point rather than an implementation detail: on
    // Linux/glibc a newly created thread INHERITS the creator's MXCSR (clone
    // copies the FPU state), whereas Windows gives it a fresh default. Spawning
    // after the change therefore proves nothing portable -- and worse, it models
    // the wrong scenario, since a host's audio thread is not spawned by our
    // setup call. Sequencing it correctly is both faithful and platform-neutral.
    const AmbientDenormalModeRestorer restoreAmbient;

    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_OFF);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_OFF);

    std::mutex m;
    std::condition_variable cv;
    bool mainThreadHasSetFtz = false;
    bool workerObservedFlush = true;
    bool workerFlushesUnderGuard = false;

    std::thread worker([&] {
        // Running with the default FP environment before the main thread acts.
        std::unique_lock<std::mutex> lock(m);
        cv.wait(lock, [&] { return mainThreadHasSetFtz; });
        lock.unlock();

        // The main thread's FTZ change happened after this thread started, so
        // it must be invisible here -- exactly the setupProcessing() failure.
        workerObservedFlush = denormalsAreFlushed();

        const ScopedDenormalMode guard;
        workerFlushesUnderGuard = denormalsAreFlushed();
    });

    // Stand in for setupProcessing() running on the main thread.
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
    REQUIRE(denormalsAreFlushed()); // in effect *here*...
    {
        std::lock_guard<std::mutex> lock(m);
        mainThreadHasSetFtz = true;
    }
    cv.notify_one();
    worker.join();

    CHECK_FALSE(workerObservedFlush);     // ...but not on the audio thread
    CHECK(workerFlushesUnderGuard);       // ...until the guard runs there
}

#else

TEST_CASE("ScopedDenormalMode is a no-op on non-x86 targets",
          "[dsp][core][denormal]")
{
    const ScopedDenormalMode guard;
    SUCCEED("No SSE denormal control on this target; guard compiles to nothing.");
}

#endif
