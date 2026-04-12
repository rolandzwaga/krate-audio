// ==============================================================================
// Coupling Fuzz Test -- SC-009 zero audio-thread allocations
// ==============================================================================
// T059b: Drives the VoicePool (with an attached SympatheticResonance coupling
// engine, mirroring how the Processor sets it up) with a deterministic
// pseudo-random schedule of MIDI noteOn/noteOff events covering all 32 pads
// for a 10-second equivalent at 44.1 kHz with 512-sample blocks. All
// allocations during the fuzz run are tracked via
// TestHelpers::AllocationDetector and asserted to be zero (SC-009).
//
// This targets the audio-thread-critical code paths exercised by coupling:
// voice allocation + stealing, coupling engine noteOn/noteOff, coupling
// signal chain (mono sum + delay read/write + engine.process()), and the
// energy limiter -- which is precisely the same per-block work the Processor
// dispatches in its own process() (see processor.cpp lines 555-655).
//
// The operator new/delete overrides that back AllocationDetector live in
// test_allocation_matrix.cpp -- ODR-safe since they are the only overrides in
// the membrum_tests binary.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "voice_pool/voice_pool.h"
#include "dsp/pad_config.h"
#include "dsp/exciter_type.h"
#include "dsp/body_model_type.h"
#include "../voice_pool/voice_pool_test_helpers.h"

#include <krate/dsp/primitives/delay_line.h>
#include <krate/dsp/systems/sympathetic_resonance.h>

#include <allocation_detector.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int    kBlockSize  = 512;
constexpr int    kNumPads    = Membrum::kNumPads; // 32

// 10 seconds worth of blocks -- SC-009 acceptance criterion.
constexpr int    kNumBlocks  = static_cast<int>(kSampleRate * 10.0) / kBlockSize;

// MIDI notes 36-67 map to pads 0-31.
constexpr std::uint8_t kFirstNote = 36;

inline bool isFiniteSample(float x) noexcept
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &x, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

// Simple, allocation-free energy limiter mirroring the one in
// processor.cpp (::applyEnergyLimiter) so the fuzz test exercises the same
// per-sample work.
inline float fuzzEnergyLimiter(float sample, float& envelope) noexcept
{
    constexpr float kThreshold = 0.1f;  // -20 dBFS
    constexpr float kAttack  = 0.01f;
    constexpr float kRelease = 0.001f;
    const float absSample = std::fabs(sample);
    if (absSample > envelope)
        envelope += (absSample - envelope) * kAttack;
    else
        envelope += (absSample - envelope) * kRelease;
    const float gain = (envelope > kThreshold)
        ? (kThreshold / envelope)
        : 1.0f;
    return sample * gain;
}

} // namespace

TEST_CASE("Coupling fuzz: SC-009 zero audio-thread allocations under random MIDI load",
          "[coupling][rt-safety][fuzz]")
{
    // ---- Assemble the coupling rig OUTSIDE the tracking scope. ----
    // Everything allocated here (unique_ptrs, output vectors, etc.) is
    // paid for up-front; the fuzz loop must not allocate.
    Krate::DSP::SympatheticResonance couplingEngine;
    couplingEngine.prepare(kSampleRate);
    couplingEngine.setAmount(1.0f);  // Global Coupling = 100%

    Krate::DSP::DelayLine couplingDelay;
    couplingDelay.prepare(kSampleRate, 0.002f);  // 2 ms max, as in Processor

    Membrum::VoicePool pool;
    pool.prepare(kSampleRate, kBlockSize);
    pool.setMaxPolyphony(16);
    pool.setVoiceStealingPolicy(Membrum::VoiceStealingPolicy::Oldest);
    pool.setCouplingEngine(&couplingEngine);

    // Populate pad configs so every pad is valid and every pad contributes
    // to coupling (per-pad couplingAmount = 1.0 -> resonators register).
    Membrum::TestHelpers::setAllPadsVoiceParams(pool, 0.5f, 0.5f, 0.3f, 0.3f, 0.8f);
    Membrum::TestHelpers::setAllPadsExciterType(pool, Membrum::ExciterType::Impulse);
    Membrum::TestHelpers::setAllPadsBodyModel(pool, Membrum::BodyModelType::Membrane);
    for (int p = 0; p < kNumPads; ++p)
    {
        pool.setPadConfigField(p, Membrum::kPadCouplingAmount, 1.0f);
    }
    // No global choke group -- lets voice stealing paths exercise too.
    pool.setChokeGroup(0);

    std::vector<float> outL(static_cast<size_t>(kBlockSize), 0.0f);
    std::vector<float> outR(static_cast<size_t>(kBlockSize), 0.0f);
    float energyEnvelope = 0.0f;

    // ---- WARM-UP (outside tracking scope) ----
    // Fire every pad multiple times so every voice slot has been stolen and
    // re-allocated for every body/exciter combo, coupling resonators have
    // been registered + reclaimed, and all first-touch lazy-init paths
    // have completed.
    for (int round = 0; round < 3; ++round)
    {
        for (int p = 0; p < kNumPads; ++p)
        {
            pool.noteOn(static_cast<std::uint8_t>(kFirstNote + p), 0.8f);
        }
        for (int b = 0; b < 16; ++b)
        {
            pool.processBlock(outL.data(), outR.data(), kBlockSize);
            for (int s = 0; s < kBlockSize; ++s)
            {
                const float mono = (outL[static_cast<size_t>(s)] +
                                    outR[static_cast<size_t>(s)]) * 0.5f;
                const float delayed = couplingDelay.readLinear(
                    1.0f * static_cast<float>(kSampleRate) * 0.001f);
                couplingDelay.write(mono);
                const float c = fuzzEnergyLimiter(couplingEngine.process(delayed),
                                                  energyEnvelope);
                outL[static_cast<size_t>(s)] += c;
                outR[static_cast<size_t>(s)] += c;
            }
        }
        for (int p = 0; p < kNumPads; ++p)
        {
            pool.noteOff(static_cast<std::uint8_t>(kFirstNote + p));
        }
        for (int b = 0; b < 16; ++b)
        {
            pool.processBlock(outL.data(), outR.data(), kBlockSize);
            for (int s = 0; s < kBlockSize; ++s)
            {
                const float mono = (outL[static_cast<size_t>(s)] +
                                    outR[static_cast<size_t>(s)]) * 0.5f;
                const float delayed = couplingDelay.readLinear(
                    1.0f * static_cast<float>(kSampleRate) * 0.001f);
                couplingDelay.write(mono);
                const float c = fuzzEnergyLimiter(couplingEngine.process(delayed),
                                                  energyEnvelope);
                outL[static_cast<size_t>(s)] += c;
                outR[static_cast<size_t>(s)] += c;
            }
        }
    }

    // Deterministic xorshift for a reproducible event schedule that covers
    // all 32 pads with a mix of noteOn / noteOff events.
    auto xorshift = [](std::uint32_t& s) {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return s;
    };
    std::uint32_t rng = 0xC0FFEEu;

    // ---- TRACKING SCOPE ----
    auto& detector = TestHelpers::AllocationDetector::instance();
    detector.startTracking();

    bool anyNonFinite = false;
    int noteOnCount = 0;
    int noteOffCount = 0;

    for (int b = 0; b < kNumBlocks; ++b)
    {
        // Per-block: 0..4 random MIDI events, covering all pads.
        const int numEvents = static_cast<int>(xorshift(rng) % 5u);
        for (int e = 0; e < numEvents; ++e)
        {
            const std::uint8_t padIdx =
                static_cast<std::uint8_t>(xorshift(rng) % kNumPads);
            const std::uint8_t note =
                static_cast<std::uint8_t>(kFirstNote + padIdx);
            const std::uint32_t kind = xorshift(rng) % 3u;
            if (kind == 2u)
            {
                pool.noteOff(note);
                ++noteOffCount;
            }
            else
            {
                const float vel = 0.1f +
                    0.9f * static_cast<float>(xorshift(rng) & 0xFFu) / 255.0f;
                pool.noteOn(note, vel);
                ++noteOnCount;
            }
        }

        // Process one block of audio.
        pool.processBlock(outL.data(), outR.data(), kBlockSize);

        // Coupling signal chain (mirrors Processor::process lines 632-647).
        for (int s = 0; s < kBlockSize; ++s)
        {
            const float mono = (outL[static_cast<size_t>(s)] +
                                outR[static_cast<size_t>(s)]) * 0.5f;
            const float delayed = couplingDelay.readLinear(
                1.0f * static_cast<float>(kSampleRate) * 0.001f);
            couplingDelay.write(mono);
            float coupling = couplingEngine.process(delayed);
            coupling = fuzzEnergyLimiter(coupling, energyEnvelope);
            outL[static_cast<size_t>(s)] += coupling;
            outR[static_cast<size_t>(s)] += coupling;

            if (!isFiniteSample(outL[static_cast<size_t>(s)])) anyNonFinite = true;
            if (!isFiniteSample(outR[static_cast<size_t>(s)])) anyNonFinite = true;
        }
    }

    const size_t allocCount = detector.stopTracking();

    CAPTURE(allocCount, kNumBlocks, noteOnCount, noteOffCount);
    // SC-009: zero heap allocations on the audio thread.
    REQUIRE(allocCount == 0u);
    // Sanity: we actually exercised coupling with plenty of events.
    REQUIRE(noteOnCount > 100);
    REQUIRE_FALSE(anyNonFinite);
}
