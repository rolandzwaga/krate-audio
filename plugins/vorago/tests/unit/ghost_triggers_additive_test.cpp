// ==============================================================================
// Vorago Phase 14 - ghost event triggers add to the atmosphere density scheduler; FR-061; filled by T005
// ==============================================================================
// ENFORCING test (Clarification Q8): the behaviour already ships, so this case
// is expected green at once. A red result is an FR-017 finding - the shipped
// behaviour differs from Q8 - and is reported, never "fixed" in
// atmosphere_engine.h.
//
// Why testing the AtmosphereEngine alone covers Vorago's ghost path:
//   - VoragoEngine::setGhostEventTriggers() writes only the flag and the latch
//     (dsp/include/krate/dsp/systems/vorago_engine.h:1004-1010);
//   - the trigger site only calls atmos_.triggerGrain() (vorago_engine.h:1530-1537);
//   - vorago_engine.h:369 is the only ghost setDensity() writer (0.30), so the
//     density scheduler runs the same whether triggers are on or off.
// The Vorago half therefore adds nothing to the scheduler; the question is
// whether AtmosphereEngine births a triggered grain ON TOP of the scheduled
// ones (atmosphere_engine.h:2391-2396: scheduler first, trigger second, each
// through birthAndTrack) rather than instead of them.
//
// Both engines are configured exactly as VoragoEngine::prepare configures
// atmos_ (vorago_engine.h:361-374) with the VoragoEngineConfig{} defaults
// (vorago_engine.h:121-126), and fed identical deterministic excitation.
// ==============================================================================

#include <krate/dsp/systems/atmosphere_engine.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace {

using Krate::DSP::AtmosphereEngine;

constexpr double kSampleRate = 48000.0;
constexpr std::size_t kBlock = 512;
constexpr std::uint32_t kSeed = 0x5EEDu;

[[nodiscard]] std::unique_ptr<AtmosphereEngine> makeGhostAtmosphere() {
    auto atmos = std::make_unique<AtmosphereEngine>();
    atmos->setSeed(kSeed);
    atmos->prepare(kSampleRate, AtmosphereEngine::PrepareConfig{.captureSeconds = 20.0f,
                                                                .blurEnabled = true,
                                                                .freezeEnabled = false,
                                                                .blurFftSize = 1024,
                                                                .freezeFftSize = 2048,
                                                                .maxBlockSamples = kBlock});
    atmos->setDensity(0.30f);
    atmos->setGrainSeconds(12.0f);
    atmos->setPitchSemitones(-12.0f);
    atmos->setPositionSpread(0.90f);
    atmos->setBlur(0.85f);
    atmos->setDecorrelation(0.85f);
    atmos->setGrainReverseProbability(0.0f);
    return atmos;
}

struct Counters {
    std::uint64_t born = 0;
    std::uint64_t triggeredBorn = 0;
    std::uint64_t skipPoolFull = 0;
    std::uint64_t skipRingCold = 0;
};

[[nodiscard]] Counters snapshot(const AtmosphereEngine& atmos) {
    return Counters{.born = atmos.getTotalGrainsBorn(),
                    .triggeredBorn = atmos.getTotalTriggeredGrainsBorn(),
                    .skipPoolFull = atmos.getSkippedTriggerCountPoolFull(),
                    .skipRingCold = atmos.getSkippedTriggerCountRingCold()};
}

}  // namespace

TEST_CASE("Vorago_Ghost_TriggersAddToDensityScheduler", "[vorago][ghost]") {
    auto off = makeGhostAtmosphere();
    auto on = makeGhostAtmosphere();

    constexpr double kTwoPi = 6.283185307179586;
    constexpr double kFreqHz = 65.4;
    const float amp = static_cast<float>(std::pow(10.0, -12.0 / 20.0));  // -12 dBFS
    const double phaseInc = kTwoPi * kFreqHz / kSampleRate;
    double phase = 0.0;

    std::vector<float> inL(kBlock);
    std::vector<float> inR(kBlock);
    std::vector<float> outOffL(kBlock);
    std::vector<float> outOffR(kBlock);
    std::vector<float> outOnL(kBlock);
    std::vector<float> outOnR(kBlock);

    const auto renderBlock = [&]() {
        for (std::size_t i = 0; i < kBlock; ++i) {
            const float s = amp * static_cast<float>(std::sin(phase));
            inL[i] = s;
            inR[i] = s;
            phase += phaseInc;
            if (phase >= kTwoPi) {
                phase -= kTwoPi;
            }
        }
        off->processStereoBlock(inL.data(), inR.data(), outOffL.data(), outOffR.data(), kBlock);
        on->processStereoBlock(inL.data(), inR.data(), outOnL.data(), outOnR.data(), kBlock);
    };

    // 20 s warm-up: fills the 20 s capture ring. No triggers in either arm.
    const auto warmupSamples = static_cast<std::uint64_t>(20.0 * kSampleRate);
    for (std::uint64_t s = 0; s < warmupSamples; s += kBlock) {
        renderBlock();
    }

    const Counters offBefore = snapshot(*off);
    const Counters onBefore = snapshot(*on);

    // 60 s span. Arm On gets N = 12 triggerGrain() calls, one every 5 s
    // (span offsets 0, 5, ..., 55 s), each issued before the block that
    // starts at or after its time; arm Off never triggers.
    constexpr int kNumTriggers = 12;
    const auto spanSamples = static_cast<std::uint64_t>(60.0 * kSampleRate);
    const auto triggerInterval = static_cast<std::uint64_t>(5.0 * kSampleRate);
    int triggersIssued = 0;
    for (std::uint64_t s = 0; s < spanSamples; s += kBlock) {
        if (triggersIssued < kNumTriggers &&
            s >= static_cast<std::uint64_t>(triggersIssued) * triggerInterval) {
            on->triggerGrain();
            ++triggersIssued;
        }
        renderBlock();
    }
    REQUIRE(triggersIssued == kNumTriggers);

    const Counters offAfter = snapshot(*off);
    const Counters onAfter = snapshot(*on);

    // Zero skipped-trigger deltas in BOTH arms: no birth lost to a full pool
    // or a cold ring, so the scheduled-birth comparison below is exact.
    CHECK(offAfter.skipPoolFull - offBefore.skipPoolFull == 0u);
    CHECK(offAfter.skipRingCold - offBefore.skipRingCold == 0u);
    CHECK(onAfter.skipPoolFull - onBefore.skipPoolFull == 0u);
    CHECK(onAfter.skipRingCold - onBefore.skipRingCold == 0u);

    // Every trigger became a grain.
    CHECK(onAfter.triggeredBorn - onBefore.triggeredBorn == static_cast<std::uint64_t>(kNumTriggers));
    CHECK(offAfter.triggeredBorn - offBefore.triggeredBorn == 0u);

    // The density scheduler births the same grains with or without triggers:
    // triggered grains ADD on top of it, they do not replace scheduled ones.
    const std::uint64_t offScheduled = offAfter.born - offBefore.born;
    const std::uint64_t onScheduled =
        (onAfter.born - onAfter.triggeredBorn) - (onBefore.born - onBefore.triggeredBorn);
    CHECK(offScheduled > 0u);
    CHECK(onScheduled == offScheduled);
}
