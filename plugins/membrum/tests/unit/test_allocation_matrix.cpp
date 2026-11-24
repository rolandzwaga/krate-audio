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

// Global allocation-operator replacements live in ONE shared header so the
// matched set and its visibility cannot drift per-TU. See that header for the
// two broken shapes this repo already shipped and the ASan numbers behind them.
#include <allocation_operator_overrides.h>

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
// MB-136 positive control: this binary's detector counts an allocation made
// inside a still-open AllocationScope. Without it every scope-based
// zero-allocation CHECK in membrum_tests could pass while counting nothing.
// ==============================================================================
TEST_CASE("AllocationScope counts inside an open scope in membrum_tests", "[allocation]")
{
    const TestHelpers::AllocationScope scope;
    TestHelpers::allocateObservably();
    REQUIRE(scope.getAllocationCount() >= 1);
    REQUIRE(scope.hadAllocations());
}

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

            // Warm-up noteOn outside the tracked scope: it applies the pending
            // exciter/body types, so the scopes below measure the steady-state
            // audio-thread path of this combo. The type swap itself (the first
            // noteOn after a type change) is measured by "AllocationMatrix: a
            // noteOn that swaps the exciter or body type allocates nothing".
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

// ==============================================================================
// MB-002 / MB-020: a voice slot serves every pad, so a noteOn whose pad has a
// different exciter or body type than the slot's previous pad swaps the type
// on the audio thread. That swap must not allocate (no FMOperator wavetable
// build, no comb or waveguide delay-line resize, no free of the old type).
// ==============================================================================
namespace {

// Plays type A to a quiescent tail, switches to type B, and returns the
// allocation count of the swapping noteOn plus one 512-sample block.
size_t allocationsOfSwap(Membrum::ExciterType exA, Membrum::BodyModelType bodyA,
                         Membrum::ExciterType exB, Membrum::BodyModelType bodyB)
{
    Membrum::DrumVoice voice;
    voice.prepare(48000.0, 0u);
    voice.setMaterial(0.5f);
    voice.setSize(0.5f);
    voice.setDecay(0.5f);
    voice.setStrikePosition(0.3f);
    voice.setLevel(0.8f);
    voice.setExciterType(exA);
    voice.setBodyModel(bodyA);

    voice.noteOn(kVelocity);
    std::array<float, 64> small{};
    voice.processBlock(small.data(), 64);
    voice.noteOff();
    for (int i = 0; i < 200; ++i)
        voice.processBlock(small.data(), 64);

    voice.setExciterType(exB);
    voice.setBodyModel(bodyB);

    std::array<float, 512> block{};
    TestHelpers::AllocationScope scope;
    voice.noteOn(kVelocity);
    voice.processBlock(block.data(), 512);
    return scope.getAllocationCount();
}

} // namespace

TEST_CASE("AllocationMatrix: a noteOn that swaps the exciter or body type allocates nothing",
          "[allocation][rt-safety]")
{
    constexpr int kNumExciters = static_cast<int>(Membrum::ExciterType::kCount);
    constexpr int kNumBodies   = static_cast<int>(Membrum::BodyModelType::kCount);

    int exciterPairs = 0;
    for (int a = 0; a < kNumExciters; ++a)
    {
        for (int b = 0; b < kNumExciters; ++b)
        {
            if (a == b)
                continue;
            ++exciterPairs;
            const auto exA = static_cast<Membrum::ExciterType>(a);
            const auto exB = static_cast<Membrum::ExciterType>(b);
            const size_t count = allocationsOfSwap(exA, Membrum::BodyModelType::Membrane,
                                                   exB, Membrum::BodyModelType::Membrane);
            INFO("exciter swap " << exciterName(exA) << " -> " << exciterName(exB)
                 << " (Membrane body): " << count << " allocations");
            CHECK(count == 0u);
        }
    }

    int bodyPairs = 0;
    for (int a = 0; a < kNumBodies; ++a)
    {
        for (int b = 0; b < kNumBodies; ++b)
        {
            if (a == b)
                continue;
            ++bodyPairs;
            const auto bodyA = static_cast<Membrum::BodyModelType>(a);
            const auto bodyB = static_cast<Membrum::BodyModelType>(b);
            const size_t count = allocationsOfSwap(Membrum::ExciterType::Impulse, bodyA,
                                                   Membrum::ExciterType::Impulse, bodyB);
            INFO("body swap " << bodyName(bodyA) << " -> " << bodyName(bodyB)
                 << " (Impulse exciter): " << count << " allocations");
            CHECK(count == 0u);
        }
    }

    CHECK(exciterPairs == 42);
    CHECK(bodyPairs == 30);
}
