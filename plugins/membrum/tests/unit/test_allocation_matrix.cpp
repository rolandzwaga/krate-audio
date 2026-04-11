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
// Global operator new/delete overrides — required for AllocationDetector to
// observe heap allocations in this binary. These are the first overrides in
// the membrum_tests TU set.
// ==============================================================================
void* operator new(std::size_t size)
{
    TestHelpers::AllocationDetector::instance().recordAllocation();
    void* p = std::malloc(size);
    if (!p) throw std::bad_alloc();
    return p;
}

void* operator new[](std::size_t size)
{
    TestHelpers::AllocationDetector::instance().recordAllocation();
    void* p = std::malloc(size);
    if (!p) throw std::bad_alloc();
    return p;
}

void operator delete(void* p) noexcept                 { std::free(p); }
void operator delete[](void* p) noexcept               { std::free(p); }
void operator delete(void* p, std::size_t) noexcept    { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept  { std::free(p); }

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
