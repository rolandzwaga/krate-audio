// ==============================================================================
// Allocation matrix tests (Phase 5 — T075)
// ==============================================================================
// Covers:
//   - SC-011: Zero allocations reported by allocation_detector for
//     DrumVoice::noteOn(), DrumVoice::noteOff(), DrumVoice::process() across
//     all 36 exciter × body combinations.
//   - FR-072: All DSP on the audio thread is allocation-free, lock-free,
//     exception-free.
//
// This translation unit overrides global operator new / delete so that any
// heap allocation made inside a TestHelpers::AllocationScope is counted.
// No other membrum test TU overrides these (verified by
// `rg "operator new" plugins/membrum/tests` returning nothing before this
// file was added), so there is no ODR collision in the membrum_tests binary.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "dsp/body_model_type.h"
#include "dsp/drum_voice.h"
#include "dsp/exciter_type.h"

#include <allocation_detector.h>

#include <array>
#include <cstdlib>
#include <new>
#include <string>

// ==============================================================================
// Global operator new overrides — required for AllocationDetector to observe
// heap allocations in this binary. These are the only allocation-operator
// overrides in the membrum_tests TU set.
//
// Replacing these is only well-defined if the WHOLE matched set is replaced and
// every replacement is visible program-wide. Two earlier shapes both corrupted
// the heap and surfaced as an intermittent, test-order-dependent SIGSEGV on the
// Linux CI leg:
//
//   1. new -> malloc together with a delete -> free override, compiled under
//      -fvisibility=hidden. Hidden symbols do NOT interpose libstdc++, so a
//      std::string allocated inside libstdc++ but destroyed in executable code
//      was freed by this TU's free() while libstdc++ had allocated it.
//   2. Replacing ONLY new / new[] and leaving operator delete at the library
//      default. That looks safe on glibc (default delete does call free), but
//      [new.delete] requires a replaced operator new to be paired with a
//      replaced operator delete: the pair must agree on which allocator owns
//      the block. AddressSanitizer reports it directly --
//      "alloc-dealloc-mismatch (malloc vs operator delete)" -- raised during
//      Catch2's own static test registration, i.e. before any test body runs.
//      Whatever runs next inherits a poisoned allocator, which is why the crash
//      lands on an arbitrary later test (CI died in the voice-pool steal test)
//      and never reproduces in isolation.
//
// The fix is to do it properly: replace the entire set -- throwing, nothrow,
// array and sized forms -- all on malloc/free so new and delete always agree,
// and give them default visibility so exactly one definition serves the whole
// process, libstdc++ included. With one allocator and one matched set there is
// no cross-module mismatch left to hit.
// ==============================================================================
#if defined(_MSC_VER)
#  define KRATE_ALLOC_REPLACEMENT
#else
#  define KRATE_ALLOC_REPLACEMENT __attribute__((visibility("default")))
#endif

KRATE_ALLOC_REPLACEMENT void* operator new(std::size_t size)
{
    TestHelpers::AllocationDetector::instance().recordAllocation();
    void* p = std::malloc(size ? size : 1);
    if (!p) throw std::bad_alloc();
    return p;
}

KRATE_ALLOC_REPLACEMENT void* operator new[](std::size_t size)
{
    TestHelpers::AllocationDetector::instance().recordAllocation();
    void* p = std::malloc(size ? size : 1);
    if (!p) throw std::bad_alloc();
    return p;
}

KRATE_ALLOC_REPLACEMENT void* operator new(std::size_t size,
                                           const std::nothrow_t&) noexcept
{
    TestHelpers::AllocationDetector::instance().recordAllocation();
    return std::malloc(size ? size : 1);
}

KRATE_ALLOC_REPLACEMENT void* operator new[](std::size_t size,
                                             const std::nothrow_t&) noexcept
{
    TestHelpers::AllocationDetector::instance().recordAllocation();
    return std::malloc(size ? size : 1);
}

KRATE_ALLOC_REPLACEMENT void operator delete(void* p) noexcept { std::free(p); }
KRATE_ALLOC_REPLACEMENT void operator delete[](void* p) noexcept { std::free(p); }

KRATE_ALLOC_REPLACEMENT void operator delete(void* p, std::size_t) noexcept
{
    std::free(p);
}

KRATE_ALLOC_REPLACEMENT void operator delete[](void* p, std::size_t) noexcept
{
    std::free(p);
}

KRATE_ALLOC_REPLACEMENT void operator delete(void* p, const std::nothrow_t&) noexcept
{
    std::free(p);
}

KRATE_ALLOC_REPLACEMENT void operator delete[](void* p, const std::nothrow_t&) noexcept
{
    std::free(p);
}

namespace {

constexpr double kSampleRate = 44100.0;
constexpr float  kVelocity = 100.0f / 127.0f;

constexpr const char* exciterName(Membrum::ExciterType t) noexcept
{
    switch (t)
    {
    case Membrum::ExciterType::Impulse:    return "Impulse";
    case Membrum::ExciterType::Mallet:     return "Mallet";
    case Membrum::ExciterType::NoiseBurst: return "NoiseBurst";
    case Membrum::ExciterType::Friction:   return "Friction";
    case Membrum::ExciterType::FMImpulse:  return "FMImpulse";
    case Membrum::ExciterType::Feedback:   return "Feedback";
    case Membrum::ExciterType::Clap:       return "Clap";
    default:                               return "Unknown";
    }
}

constexpr const char* bodyName(Membrum::BodyModelType t) noexcept
{
    switch (t)
    {
    case Membrum::BodyModelType::Membrane:  return "Membrane";
    case Membrum::BodyModelType::Plate:     return "Plate";
    case Membrum::BodyModelType::Shell:     return "Shell";
    case Membrum::BodyModelType::String:    return "String";
    case Membrum::BodyModelType::Bell:      return "Bell";
    case Membrum::BodyModelType::NoiseBody: return "NoiseBody";
    default:                                return "Unknown";
    }
}

} // namespace

// ==============================================================================
// SC-011: allocation_detector wraps noteOn / process / noteOff across all 36
// combinations — must report zero heap allocations for every entry point.
//
// DrumVoice is prepare()d BEFORE the AllocationScope opens, so any
// preparation-time allocations (e.g. ModalResonatorBank::prepare configuring
// internal buffers) are not counted — only the audio-thread entry points are
// under scrutiny. FR-006 explicitly permits prepare-time allocations.
// ==============================================================================
TEST_CASE("AllocationMatrix: zero heap allocs on audio-thread entries for all 36 combos",
          "[membrum][allocation][phase5]")
{
    constexpr int kNumExciters = static_cast<int>(Membrum::ExciterType::kCount);
    constexpr int kNumBodies   = static_cast<int>(Membrum::BodyModelType::kCount);

    for (int e = 0; e < kNumExciters; ++e)
    {
        for (int b = 0; b < kNumBodies; ++b)
        {
            const auto ex   = static_cast<Membrum::ExciterType>(e);
            const auto body = static_cast<Membrum::BodyModelType>(b);

            Membrum::DrumVoice voice;
            voice.prepare(kSampleRate, 0u);
            voice.setMaterial(0.5f);
            voice.setSize(0.5f);
            voice.setDecay(0.5f);
            voice.setStrikePosition(0.3f);
            voice.setLevel(0.8f);
            voice.setExciterType(ex);
            voice.setBodyModel(body);

            // "Warm up" the variant swap: call noteOn once BEFORE the tracked
            // scope so the pending exciter/body types are applied during an
            // untracked noteOn. This matches the FR-072 contract: the first
            // note after a type change is where the swap happens, and the
            // swap itself uses std::variant::emplace which is guaranteed not
            // to allocate for our in-place types. We still want to measure
            // the *steady-state* audio-thread path too.
            voice.noteOn(kVelocity);
            // Process a small chunk to get past transient branches.
            {
                std::array<float, 64> warmup{};
                voice.processBlock(warmup.data(), 64);
            }
            voice.noteOff();
            // Drain envelope release so the voice is quiescent.
            {
                std::array<float, 64> drain{};
                for (int i = 0; i < 200; ++i) // ~290 ms @ 44.1 kHz
                    voice.processBlock(drain.data(), 64);
            }

            const std::string label =
                std::string(exciterName(ex)) + " + " + bodyName(body);

            // --- noteOn() must not allocate ---------------------------------
            {
                TestHelpers::AllocationScope scope;
                voice.noteOn(kVelocity);
                const size_t count = scope.getAllocationCount();
                INFO("noteOn() alloc count for " << label << " = " << count);
                CHECK(count == 0u);
            }

            // --- process() block must not allocate --------------------------
            {
                std::array<float, 512> block{};
                TestHelpers::AllocationScope scope;
                voice.processBlock(block.data(), 512);
                const size_t count = scope.getAllocationCount();
                INFO("processBlock() alloc count for " << label << " = " << count);
                CHECK(count == 0u);
            }

            // --- noteOff() must not allocate --------------------------------
            {
                TestHelpers::AllocationScope scope;
                voice.noteOff();
                const size_t count = scope.getAllocationCount();
                INFO("noteOff() alloc count for " << label << " = " << count);
                CHECK(count == 0u);
            }

            // --- per-sample process() path must not allocate either --------
            {
                voice.noteOn(kVelocity);
                TestHelpers::AllocationScope scope;
                for (int i = 0; i < 256; ++i)
                    (void) voice.process();
                const size_t count = scope.getAllocationCount();
                INFO("process() alloc count for " << label << " = " << count);
                CHECK(count == 0u);
                voice.noteOff();
            }
        }
    }
}
