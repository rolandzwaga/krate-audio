// =============================================================================
// Layer 3: System Tests - ResonanceDriftNetwork non-finite hygiene (SC-009)
//                              (specs/vorago-phase3-resonance-drift)
// =============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/vorago-phase3-resonance-drift/spec.md   (SC-009, FR-008,
//                                                           FR-009)
//            specs/vorago-phase3-resonance-drift/plan.md   (S1.3 - the setter
//                                                           declaration order)
//            specs/vorago-phase3-resonance-drift/tasks.md  (T001 creates this
//                                                           TU, T014 lands this
//                                                           case)
//
// SCOPE OF THIS TU: SC-009 ONLY.
//
// THIS IS A SEPARATE TU BECAUSE OF ITS COMPILE FLAGS. It is the ONLY one of the
//   four Phase 3 TUs listed under "-fno-fast-math -fno-finite-math-only" in
//   dsp/tests/CMakeLists.txt:820. The other three must NOT be added to that
//   block: resonance_drift_network_test.cpp and
//   resonance_drift_network_spectral_test.cpp stay out so the FR-008/FR-009
//   guards are also exercised in the /fp:fast + -ffast-math mode the header
//   actually ships in, and resonance_drift_network_perf_test.cpp stays out
//   because -fno-fast-math would move the figures its baselines are pinned to.
//
// WHY THIS IS A REAL TRACE AND NOT A FORMALITY. std::clamp does NOT reject NaN:
//   with v = NaN both `v < lo` and `hi < v` are false, so v is returned
//   unchanged. A clamp-only setter therefore admits NaN into configuration
//   state, and the trace the header records as NORMATIVE
//   (resonance_drift_network.h:474-487) is fatal:
//     setPeakAnchorHz(i, NaN) -> freeAnchorHz = NaN -> anchorLog2Hz = NaN
//       -> appliedHz = NaN -> ResonatorBank::setFrequency clamps with a bare
//          std::clamp (resonator_bank.h:631-635)
//       -> omega NaN -> sin/cos NaN -> NaN biquad coefficients. Biquad::process
//          resets only on a non-finite INPUT SAMPLE, never on non-finite
//          COEFFICIENTS, so that resonator emits NaN forever - through FR-018's
//          output clamp, which is itself a std::clamp and propagates NaN.
//
//   FR-008's remedy is REJECTION, not substitution: `if (!detail::isFinite(v))
//   return;` as the first statement of every float setter, so the PREVIOUS
//   value stands. This is a deliberate difference from NoiseOrganism::sanitise,
//   which substitutes a per-argument neutral (noise_organism.h:1099-1101).
//   prepare()'s `double sampleRate` is the one exception the spec names: it is
//   SUBSTITUTED by 48000 and then floored at kMinUsableSampleRate, because
//   there is no previous rate to fall back to
//   (resonance_drift_network.h:241-242).
//
//   FR-009 covers the audio path: each input channel is sanitised
//   INDEPENDENTLY, before the mono sum and before the dry capture
//   (resonance_drift_network.h:1605-1607), so a non-finite sample on one
//   channel can neither reach the engine nor cross into the other channel's
//   dry path.
//
// NON-FINITE VALUES ARE BUILT FROM BIT PATTERNS THROUGH A VOLATILE SINK, never
//   from std::numeric_limits<float>::quiet_NaN() / infinity(): those fold to
//   FINITE GARBAGE on the macOS -ffast-math leg, and a test that injects a
//   finite number proves nothing. Finiteness is read with
//   Krate::DSP::detail::isFinite (core/db_utils.h:118), which inspects the
//   IEEE-754 exponent field through an optimisation barrier - never
//   std::isnan / std::isinf / std::isfinite, which fold away in the same place
//   (tools/lint-nonfinite-symbols.js gates this).
//
// NO BIT-EXACT FLOAT GOLDENS anywhere in this TU. The `==` comparisons below
//   are "this stored value was NOT written", not "this computation reproduced a
//   pinned number" - the setter under test either stored the argument verbatim
//   or returned without touching the member, so exact equality is the whole
//   assertion (node tools/lint-float-bit-goldens.js gates the real thing).
// =============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/random.h>
#include <krate/dsp/processors/resonator_bank.h>
#include <krate/dsp/systems/resonance_drift_network.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using Catch::Approx;
using Krate::DSP::ResonanceDriftNetwork;
using Krate::DSP::Xorshift32;
using Krate::DSP::detail::isFinite;

namespace {

// =============================================================================
// Fixture constants
// =============================================================================

constexpr double kFs = 48000.0;

/// The peak every per-peak setter probe writes to. Any index < kMaxPeaks works;
/// pinning one keeps the failure message unambiguous.
constexpr std::size_t kProbePeak = 3;

/// A DELIBERATELY QUIET fixture, so arm (c) has teeth. FR-018's clamp fires at
/// +/-4.0 and bumps getClampEngagementCount(); the default +30 dB wet trim
/// (kDefaultWetGainDb) driven at a realistic level can legitimately reach it,
/// and a legitimate engagement would mask the thing arm (c) is actually
/// watching for - a non-finite value walking into the clamp. At -18 dB of trim
/// and 0.1 peak input the head-room is more than an order of magnitude, so any
/// engagement at all is a defect.
constexpr float kQuietWetGainDb = -18.0f;
constexpr float kInputPeak      = 0.1f;

/// Long enough that the FR-041/C-6 50 ms ramps (2400 samples) are well clear of
/// the measurement, and that the injected burst is a small fraction of the
/// block's energy.
constexpr std::size_t kBlockSamples = 8192;

// =============================================================================
// Non-finite construction (never std::numeric_limits)
// =============================================================================

struct NonFinitePattern {
    const char*   name;
    std::uint32_t bits;
};

/// The three binary32 patterns, named once rather than spelled at every
/// injection site.
constexpr std::array<NonFinitePattern, 3> kPatterns{{
    {"quiet NaN", 0x7FC00000u},
    {"+Inf", 0x7F800000u},
    {"-Inf", 0xFF800000u},
}};

/// The binary64 twins, for prepare()'s `double sampleRate`. Same order.
constexpr std::array<std::uint64_t, 3> kPatterns64{{
    0x7FF8000000000000ULL,  // quiet NaN
    0x7FF0000000000000ULL,  // +Inf
    0xFFF0000000000000ULL,  // -Inf
}};

/// @brief Build a non-finite float from its bit pattern through a volatile sink.
///
/// The volatile READ is the sink: it is what stops the constant from being
/// folded back into the memcpy at compile time, which is how a -ffast-math build
/// turns an "infinity" literal into a finite number. Idiom copied from
/// dsp/tests/unit/systems/noise_organism_nonfinite_test.cpp:114-120.
[[nodiscard]] float makeNonFinite(std::uint32_t bits) noexcept {
    volatile std::uint32_t sink         = bits;
    const std::uint32_t    materialized = sink;
    float                  out          = 0.0f;
    std::memcpy(&out, &materialized, sizeof(out));
    return out;
}

/// @brief The binary64 twin of makeNonFinite, for prepare()'s sampleRate.
[[nodiscard]] double makeNonFiniteDouble(std::uint64_t bits) noexcept {
    volatile std::uint64_t sink         = bits;
    const std::uint64_t    materialized = sink;
    double                 out          = 0.0;
    std::memcpy(&out, &materialized, sizeof(out));
    return out;
}

// =============================================================================
// Stereo render helpers
// =============================================================================

struct StereoBuffer {
    std::vector<float> l;
    std::vector<float> r;
};

/// @brief Deterministic bipolar noise, identical for a given seed.
[[nodiscard]] StereoBuffer makeNoise(std::size_t n, std::uint32_t seed, float peak) {
    Xorshift32   rng{seed};
    StereoBuffer buf{.l = std::vector<float>(n, 0.0f), .r = std::vector<float>(n, 0.0f)};
    for (std::size_t i = 0; i < n; ++i) {
        buf.l[i] = rng.nextFloat() * peak;  // nextFloat() is [-1, +1] (core/random.h:39)
        buf.r[i] = rng.nextFloat() * peak;
    }
    return buf;
}

/// @brief The quiet fixture of the comment above. Wander OFF so the two
///        instances of arm (a) are decoupled from lane bookkeeping entirely.
void prepareQuiet(ResonanceDriftNetwork& net) {
    net.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
    net.setSeed(0x5EED1234u);
    net.setWanderEnabled(false);
    net.setWetGain(kQuietWetGainDb);
}

[[nodiscard]] StereoBuffer renderStereo(ResonanceDriftNetwork& net, const StereoBuffer& in) {
    StereoBuffer out{.l = std::vector<float>(in.l.size(), 0.0f),
                     .r = std::vector<float>(in.r.size(), 0.0f)};
    net.processBlock(in.l.data(), in.r.data(), out.l.data(), out.r.data(), in.l.size());
    return out;
}

/// @brief Every sample on BOTH channels finite, by the fast-math-immune test.
[[nodiscard]] bool bothChannelsFinite(const StereoBuffer& buf) noexcept {
    for (const float s : buf.l) {
        if (!isFinite(s)) return false;
    }
    for (const float s : buf.r) {
        if (!isFinite(s)) return false;
    }
    return true;
}

[[nodiscard]] double rmsDb(const std::vector<float>& x) {
    double sumSq = 0.0;
    for (const float s : x) {
        sumSq += static_cast<double>(s) * static_cast<double>(s);
    }
    const double count = x.empty() ? 1.0 : static_cast<double>(x.size());
    const double rms   = std::sqrt(sumSq / count);
    return 20.0 * std::log10(rms < 1.0e-12 ? 1.0e-12 : rms);
}

enum class InjectChannel : std::uint8_t { Left, Right, Both };

[[nodiscard]] const char* channelName(InjectChannel ch) noexcept {
    switch (ch) {
        case InjectChannel::Left: return "L only";
        case InjectChannel::Right: return "R only";
        case InjectChannel::Both: return "L and R";
    }
    return "?";
}

/// @brief Overwrite `count` samples from `start` with `bad`, on the named
///        channel(s). The twin instance never sees this - it is driven with the
///        untouched finite content, which is what makes the arm (a) RMS
///        comparison a real measurement rather than a tautology.
void injectNonFinite(StereoBuffer& buf, InjectChannel ch, std::size_t start,
                     std::size_t count, float bad) {
    for (std::size_t i = start; i < start + count && i < buf.l.size(); ++i) {
        if (ch == InjectChannel::Left || ch == InjectChannel::Both) {
            buf.l[i] = bad;
        }
        if (ch == InjectChannel::Right || ch == InjectChannel::Both) {
            buf.r[i] = bad;
        }
    }
}

// =============================================================================
// Arm (b): the float setter table, in the plan S1.3 DECLARATION ORDER
// =============================================================================
// The entries below are EVERY single-argument float setter of S1.3, in S1.3's
// own declaration order. setSlewCeilings is the only float setter S1.3 declares
// that is missing here: it takes TWO arguments, and its per-argument
// independence needs a shape this table cannot express, so it is swept in a
// dedicated block after the table (out of declaration position, deliberately)
// rather than being left untested.

struct FloatSetterProbe {
    const char* name;
    void (*apply)(ResonanceDriftNetwork&, float);
    float (*read)(const ResonanceDriftNetwork&);
    float good;  ///< In range, so the getter must echo it EXACTLY after a clamp.
};

/// FIFTEEN entries - every single-argument float setter S1.3 declares, in S1.3
/// order. STATED HONESTLY: nothing here can detect a SIXTEENTH setter added to
/// the header later; that gap is closed by SC-005's touchEverySetter helper in
/// resonance_drift_network_test.cpp, not by this table. The explicit size only
/// stops an entry being dropped from THIS list by an editing accident.
const std::array<FloatSetterProbe, 15> kFloatSetters{{
    {"setPeakAnchorHz",
     [](ResonanceDriftNetwork& n, float v) { n.setPeakAnchorHz(kProbePeak, v); },
     [](const ResonanceDriftNetwork& n) { return n.getPeakAnchorHz(kProbePeak); }, 500.0f},
    {"setPeakRatio", [](ResonanceDriftNetwork& n, float v) { n.setPeakRatio(kProbePeak, v); },
     [](const ResonanceDriftNetwork& n) { return n.getPeakRatio(kProbePeak); }, 3.0f},
    {"setNoteFrequency", [](ResonanceDriftNetwork& n, float v) { n.setNoteFrequency(v); },
     [](const ResonanceDriftNetwork& n) { return n.getNoteFrequency(); }, 110.0f},
    {"setGravity", [](ResonanceDriftNetwork& n, float v) { n.setGravity(v); },
     [](const ResonanceDriftNetwork& n) { return n.getGravity(); }, 0.5f},
    {"setPeakLevel", [](ResonanceDriftNetwork& n, float v) { n.setPeakLevel(kProbePeak, v); },
     [](const ResonanceDriftNetwork& n) { return n.getPeakLevel(kProbePeak); }, -12.0f},
    {"setPeakQ", [](ResonanceDriftNetwork& n, float v) { n.setPeakQ(kProbePeak, v); },
     [](const ResonanceDriftNetwork& n) { return n.getPeakQ(kProbePeak); }, 25.0f},
    {"setFreqWander",
     [](ResonanceDriftNetwork& n, float v) { n.setFreqWander(kProbePeak, v); },
     [](const ResonanceDriftNetwork& n) { return n.getFreqWander(kProbePeak); }, 7.0f},
    {"setQWander", [](ResonanceDriftNetwork& n, float v) { n.setQWander(kProbePeak, v); },
     [](const ResonanceDriftNetwork& n) { return n.getQWander(kProbePeak); }, 1.25f},
    {"setGainWander",
     [](ResonanceDriftNetwork& n, float v) { n.setGainWander(kProbePeak, v); },
     [](const ResonanceDriftNetwork& n) { return n.getGainWander(kProbePeak); }, 9.0f},
    {"setPeakPan", [](ResonanceDriftNetwork& n, float v) { n.setPeakPan(kProbePeak, v); },
     [](const ResonanceDriftNetwork& n) { return n.getPeakPan(kProbePeak); }, -0.75f},
    {"setPeakPanWander",
     [](ResonanceDriftNetwork& n, float v) { n.setPeakPanWander(kProbePeak, v); },
     [](const ResonanceDriftNetwork& n) { return n.getPeakPanWander(kProbePeak); }, 0.6f},
    {"setWanderRate", [](ResonanceDriftNetwork& n, float v) { n.setWanderRate(v); },
     [](const ResonanceDriftNetwork& n) { return n.getWanderRate(); }, 0.25f},
    {"setPeakWake", [](ResonanceDriftNetwork& n, float v) { n.setPeakWake(kProbePeak, v); },
     [](const ResonanceDriftNetwork& n) { return n.getPeakWakeAmount(kProbePeak); }, 0.8f},
    {"setMix", [](ResonanceDriftNetwork& n, float v) { n.setMix(v); },
     [](const ResonanceDriftNetwork& n) { return n.getMix(); }, 0.4f},
    {"setWetGain", [](ResonanceDriftNetwork& n, float v) { n.setWetGain(v); },
     [](const ResonanceDriftNetwork& n) { return n.getWetGain(); }, -6.0f},
}};

/// @brief Every FR-052 realised-state read, finite for every peak.
[[nodiscard]] bool realisedStateFinite(const ResonanceDriftNetwork& net) noexcept {
    for (std::size_t i = 0; i < ResonanceDriftNetwork::kMaxPeaks; ++i) {
        if (!isFinite(net.getPeakCurrentFrequency(i))) return false;
        if (!isFinite(net.getPeakCurrentQ(i))) return false;
        if (!isFinite(net.getPeakCurrentGainDb(i))) return false;
        if (!isFinite(net.getPeakCurrentPan(i))) return false;
        if (!isFinite(net.getPeakGate(i))) return false;
        if (!isFinite(net.getPeakEquivalentRt60(i))) return false;
    }
    return true;
}

}  // namespace

// =============================================================================
// SC-009
// =============================================================================

TEST_CASE("ResonanceDriftNetwork_NonFiniteGuards", "[resonance_drift_network]") {
    // -------------------------------------------------------------------------
    // (a) FR-009: a non-finite INPUT SAMPLE, on either channel or both.
    // -------------------------------------------------------------------------
    // Two instances with identical configuration and seed. The injected one gets
    // NaN/Inf written over part of block 1; the twin gets the SAME finite noise
    // with nothing written over it. Block 2 is identical content for both, so a
    // surviving poison shows up either as a non-finite sample or as an RMS that
    // has collapsed away from the twin's.
    {
        const StereoBuffer cleanBlock1 = makeNoise(kBlockSamples, 0x11111111u, kInputPeak);
        const StereoBuffer cleanBlock2 = makeNoise(kBlockSamples, 0x22222222u, kInputPeak);

        constexpr std::array<InjectChannel, 3> kChannels{
            {InjectChannel::Left, InjectChannel::Right, InjectChannel::Both}};
        constexpr std::array<std::size_t, 2> kBursts{{std::size_t{1}, std::size_t{64}}};

        for (const NonFinitePattern& pattern : kPatterns) {
            for (const InjectChannel ch : kChannels) {
                for (const std::size_t count : kBursts) {
                    INFO("pattern=" << pattern.name << " channels=" << channelName(ch)
                                    << " burst=" << count);

                    ResonanceDriftNetwork injected;
                    ResonanceDriftNetwork twin;
                    prepareQuiet(injected);
                    prepareQuiet(twin);

                    const std::uint32_t clampsBefore = injected.getClampEngagementCount();

                    StereoBuffer poisoned = cleanBlock1;
                    injectNonFinite(poisoned, ch, kBlockSamples / 2, count,
                                    makeNonFinite(pattern.bits));

                    const StereoBuffer outInjected1 = renderStereo(injected, poisoned);
                    const StereoBuffer outTwin1     = renderStereo(twin, cleanBlock1);
                    REQUIRE(bothChannelsFinite(outInjected1));
                    REQUIRE(bothChannelsFinite(outTwin1));

                    const StereoBuffer outInjected2 = renderStereo(injected, cleanBlock2);
                    const StereoBuffer outTwin2     = renderStereo(twin, cleanBlock2);
                    REQUIRE(bothChannelsFinite(outInjected2));
                    REQUIRE(bothChannelsFinite(outTwin2));

                    // The NEXT block renders normally - not silence, and not a
                    // stuck value.
                    const double injL = rmsDb(outInjected2.l);
                    const double injR = rmsDb(outInjected2.r);
                    const double refL = rmsDb(outTwin2.l);
                    const double refR = rmsDb(outTwin2.r);
                    INFO("next-block RMS dB  injected L/R = "
                         << injL << " / " << injR << "  twin L/R = " << refL << " / " << refR);
                    REQUIRE(injL == Approx(refL).margin(0.5));
                    REQUIRE(injR == Approx(refR).margin(0.5));

                    // Realised state survives too - a poisoned coefficient would
                    // show up here even if the output clamp happened to hide it
                    // in the audio.
                    REQUIRE(realisedStateFinite(injected));

                    // (c) FR-018's counter is untouched by any of this.
                    REQUIRE(injected.getClampEngagementCount() == clampsBefore);
                }
            }
        }
    }

    // -------------------------------------------------------------------------
    // (b) FR-008: every float SETTER rejects, and the PREVIOUS value stands.
    // -------------------------------------------------------------------------
    {
        ResonanceDriftNetwork net;
        prepareQuiet(net);
        const std::uint32_t clampsBefore = net.getClampEngagementCount();

        for (const FloatSetterProbe& probe : kFloatSetters) {
            INFO("setter=" << probe.name);

            // The previous value is a DELIBERATELY NON-DEFAULT one, so a setter
            // that fell back to its FR-016 default on rejection would still fail.
            probe.apply(net, probe.good);
            REQUIRE(probe.read(net) == probe.good);

            for (const NonFinitePattern& pattern : kPatterns) {
                INFO("pattern=" << pattern.name);
                probe.apply(net, makeNonFinite(pattern.bits));
                REQUIRE(probe.read(net) == probe.good);
            }
        }

        // setWanderRate additionally owns laneDecimation_ (FR-037): a rejected
        // write must not re-derive it either.
        const std::size_t decimationBefore = net.getLaneDecimation();
        for (const NonFinitePattern& pattern : kPatterns) {
            INFO("setWanderRate decimation, pattern=" << pattern.name);
            net.setWanderRate(makeNonFinite(pattern.bits));
            REQUIRE(net.getWanderRate() == 0.25f);
            REQUIRE(net.getLaneDecimation() == decimationBefore);
        }

        // setSlewCeilings - the only two-argument float setter, rejected PER
        // ARGUMENT so one bad value cannot discard a good one
        // (resonance_drift_network.h:670-677).
        constexpr float kFreqCeiling = 0.5f;
        constexpr float kQCeiling    = 0.75f;
        net.setSlewCeilings(kFreqCeiling, kQCeiling);
        REQUIRE(net.getFreqSlewCeiling() == kFreqCeiling);
        REQUIRE(net.getQSlewCeiling() == kQCeiling);

        for (const NonFinitePattern& pattern : kPatterns) {
            INFO("setSlewCeilings, pattern=" << pattern.name);
            const float bad = makeNonFinite(pattern.bits);

            net.setSlewCeilings(bad, kQCeiling);
            REQUIRE(net.getFreqSlewCeiling() == kFreqCeiling);
            REQUIRE(net.getQSlewCeiling() == kQCeiling);

            net.setSlewCeilings(kFreqCeiling, bad);
            REQUIRE(net.getFreqSlewCeiling() == kFreqCeiling);
            REQUIRE(net.getQSlewCeiling() == kQCeiling);

            net.setSlewCeilings(bad, bad);
            REQUIRE(net.getFreqSlewCeiling() == kFreqCeiling);
            REQUIRE(net.getQSlewCeiling() == kQCeiling);

            // INDEPENDENCE: the finite half must still land.
            constexpr float kNewQCeiling = 1.5f;
            net.setSlewCeilings(bad, kNewQCeiling);
            REQUIRE(net.getFreqSlewCeiling() == kFreqCeiling);
            REQUIRE(net.getQSlewCeiling() == kNewQCeiling);
            net.setSlewCeilings(kFreqCeiling, kQCeiling);  // restore for the next pattern
        }

        // The whole rejected sweep must leave the network renderable, every
        // FR-052 read finite, and - arm (c) - the clamp counter untouched, both
        // before and after a real render.
        REQUIRE(realisedStateFinite(net));
        REQUIRE(net.getClampEngagementCount() == clampsBefore);
        const StereoBuffer after =
            renderStereo(net, makeNoise(kBlockSamples, 0x33333333u, kInputPeak));
        REQUIRE(bothChannelsFinite(after));
        REQUIRE(realisedStateFinite(net));
        REQUIRE(net.getClampEngagementCount() == clampsBefore);
    }

    // -------------------------------------------------------------------------
    // (b, continued) prepare()'s `double sampleRate` - SUBSTITUTION, not
    // rejection, because there is no previous rate to keep.
    // -------------------------------------------------------------------------
    {
        // 48000 * kMaxResonatorFrequencyRatio. The network exposes no sample-rate
        // getter, so the substituted rate is read through a value it derives: an
        // anchor above 0.45 * fs is clamped to exactly this, SILENTLY (FR-025),
        // and getPeakAnchorHz reports the clamped value. A build that had kept
        // the non-finite rate would report a non-finite anchor here.
        const double kExpectedMaxHz =
            48000.0 * static_cast<double>(Krate::DSP::kMaxResonatorFrequencyRatio);

        for (const std::uint64_t bits : kPatterns64) {
            INFO("prepare(sampleRate) bit pattern = " << std::to_string(bits));

            ResonanceDriftNetwork net;
            net.prepare(makeNonFiniteDouble(bits), ResonanceDriftNetwork::PrepareConfig{});
            net.setSeed(0x5EED1234u);
            net.setWanderEnabled(false);
            net.setWetGain(kQuietWetGainDb);

            REQUIRE(net.isPrepared());
            const std::uint32_t clampsBefore = net.getClampEngagementCount();

            net.setPeakAnchorHz(0, 30000.0f);
            REQUIRE(isFinite(net.getPeakAnchorHz(0)));
            REQUIRE(static_cast<double>(net.getPeakAnchorHz(0))
                    == Approx(kExpectedMaxHz).margin(0.01));

            REQUIRE(realisedStateFinite(net));
            const StereoBuffer out =
                renderStereo(net, makeNoise(kBlockSamples, 0x44444444u, kInputPeak));
            REQUIRE(bothChannelsFinite(out));
            REQUIRE(realisedStateFinite(net));
            REQUIRE(net.getClampEngagementCount() == clampsBefore);
        }
    }
}
