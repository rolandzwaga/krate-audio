// ==============================================================================
// Layer 3: System Tests - ResonanceDriftNetwork, behavioural cases
// ==============================================================================
// Vorago Phase 3 (specs/vorago-phase3-resonance-drift): ResonanceDriftNetwork
// behavioural cases (SC-005, SC-006, SC-007, SC-008, SC-010, SC-011, SC-014,
// SC-017, SC-018, SC-019, SC-020, SC-021, SC-022 + the clamps/clamp-engages
// cases).
//
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/vorago-phase3-resonance-drift/spec.md
//            specs/vorago-phase3-resonance-drift/plan.md
//            specs/vorago-phase3-resonance-drift/tasks.md
//
// TASKS THAT OWN CASES IN THIS TU:
//   T006  ResonanceDriftNetwork_ControlSurfaceClamps (arms (i)-(vi))
//         ResonanceDriftNetwork_PrepareFootprint      (SC-011, re-derived by C-1)
//         ResonanceDriftNetwork_RenderPathBoundaries  (arms (b)(c)(d))
//   T007  ResonanceDriftNetwork_AnchorModes, ResonanceDriftNetwork_SlewLimit
//   T008  ResonanceDriftNetwork_LaneBounds
//   T009  ResonanceDriftNetwork_BlockSizeInvariance, RenderPathBoundaries (a)(e),
//         ResonanceDriftNetwork_RendersNonSilent
//   T010  ResonanceDriftNetwork_GateRamp (SC-002 (d) and its (d-ii)/(d-iii)/
//         (d-iv) arms). SC-015's life-cycle arms live in the spectral TU.
//   T011  ResonanceDriftNetwork_PeakPan (SC-021 (a)(b)(c))
//   T012  ResonanceDriftNetwork_ResetPreservesConfiguration (FR-004 (a)(b)(c)),
//         ResonanceDriftNetwork_ClearAudioState (FR-046 (a)(b)(c))
//   T013  ResonanceDriftNetwork_OutputClampEngages (FR-018's clamp AND its
//         saturating engagement counter)
//   T017  ResonanceDriftNetwork_SeedDeterminism      (SC-006 (a)-(d))
//         ResonanceDriftNetwork_SampleRateIndependence (SC-008 (a)-(d))
//   T019  ResonanceDriftNetwork_WetGainTrim          (SC-020 (a)(b)(c)). The
//         constant it is written about, kDefaultWetGainDb, is MEASURED by
//         ResonanceDriftNetwork_MeasureThresholds in
//         resonance_drift_network_perf_test.cpp ([.calibration]); this case is
//         the compliance row, not the measurement.
//
// NON-FINITE VALUES: never std::numeric_limits<float>::quiet_NaN()/infinity()
//   here - this TU is deliberately NOT in dsp/tests/CMakeLists.txt's
//   -fno-fast-math block (tasks.md T001), so the FR-008/FR-009 guards are proved
//   in the /fp:fast + -ffast-math mode the header actually ships in. SC-009 owns
//   bit-pattern injection and lives in resonance_drift_network_nonfinite_test.cpp.
//
// ALLOCATION DETECTION: include <allocation_detector.h> ONLY. This TU must NOT
//   include <allocation_operator_overrides.h> - the single owner of the global
//   operator new/delete override in dsp_systems_tests is
//   dsp/tests/unit/systems/selectable_oscillator_test.cpp:388, and a second
//   include is a duplicate-symbol link error.
//
// NO BIT-EXACT FLOAT GOLDENS anywhere in this TU
//   (node tools/lint-float-bit-goldens.js gates it).
// ==============================================================================

#include <catch2/catch_all.hpp>

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/random.h>
#include <krate/dsp/processors/resonator_bank.h>
#include <krate/dsp/systems/resonance_drift_network.h>

#include <allocation_detector.h>
#include <render_fingerprint.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

using Krate::DSP::ResonanceDriftNetwork;

namespace {

constexpr double kFs = 48000.0;

/// Every per-peak getter plus every network-wide getter, in one comparable
/// aggregate. Arm (iv) needs "no getter ANYWHERE changes", which is only a real
/// assertion if the snapshot is exhaustive.
struct SurfaceSnapshot {
    std::size_t numPeaks{};
    int anchorMode{};
    float noteHz{};
    float gravity{};
    float wanderRate{};
    float freqSlew{};
    float qSlew{};
    bool wanderEnabled{};
    float mix{};
    float wetGain{};
    std::size_t laneDecimation{};
    std::size_t normalisationPeaks{};

    std::array<float, ResonanceDriftNetwork::kMaxPeaks> anchorHz{};
    std::array<float, ResonanceDriftNetwork::kMaxPeaks> ratio{};
    std::array<float, ResonanceDriftNetwork::kMaxPeaks> level{};
    std::array<float, ResonanceDriftNetwork::kMaxPeaks> q{};
    std::array<float, ResonanceDriftNetwork::kMaxPeaks> freqWander{};
    std::array<float, ResonanceDriftNetwork::kMaxPeaks> qWander{};
    std::array<float, ResonanceDriftNetwork::kMaxPeaks> gainWander{};
    std::array<float, ResonanceDriftNetwork::kMaxPeaks> pan{};
    std::array<float, ResonanceDriftNetwork::kMaxPeaks> panWander{};
    std::array<float, ResonanceDriftNetwork::kMaxPeaks> wake{};
    std::array<float, ResonanceDriftNetwork::kMaxPeaks> curHz{};
    std::array<float, ResonanceDriftNetwork::kMaxPeaks> curQ{};
    std::array<float, ResonanceDriftNetwork::kMaxPeaks> curGainDb{};
    std::array<float, ResonanceDriftNetwork::kMaxPeaks> curPan{};
    std::array<float, ResonanceDriftNetwork::kMaxPeaks> gate{};
    std::array<float, ResonanceDriftNetwork::kMaxPeaks> rt60{};
    std::array<bool, ResonanceDriftNetwork::kMaxPeaks> dormant{};
    std::array<bool, ResonanceDriftNetwork::kMaxPeaks> engineActive{};

    [[nodiscard]] bool operator==(const SurfaceSnapshot& o) const = default;
};

[[nodiscard]] SurfaceSnapshot snapshot(const ResonanceDriftNetwork& net) {
    SurfaceSnapshot s;
    s.numPeaks = net.getNumPeaks();
    s.anchorMode = static_cast<int>(net.getAnchorMode());
    s.noteHz = net.getNoteFrequency();
    s.gravity = net.getGravity();
    s.wanderRate = net.getWanderRate();
    s.freqSlew = net.getFreqSlewCeiling();
    s.qSlew = net.getQSlewCeiling();
    s.wanderEnabled = net.isWanderEnabled();
    s.mix = net.getMix();
    s.wetGain = net.getWetGain();
    s.laneDecimation = net.getLaneDecimation();
    s.normalisationPeaks = net.getNormalisationPeakCount();
    for (std::size_t i = 0; i < ResonanceDriftNetwork::kMaxPeaks; ++i) {
        s.anchorHz[i] = net.getPeakAnchorHz(i);
        s.ratio[i] = net.getPeakRatio(i);
        s.level[i] = net.getPeakLevel(i);
        s.q[i] = net.getPeakQ(i);
        s.freqWander[i] = net.getFreqWander(i);
        s.qWander[i] = net.getQWander(i);
        s.gainWander[i] = net.getGainWander(i);
        s.pan[i] = net.getPeakPan(i);
        s.panWander[i] = net.getPeakPanWander(i);
        s.wake[i] = net.getPeakWakeAmount(i);
        s.curHz[i] = net.getPeakCurrentFrequency(i);
        s.curQ[i] = net.getPeakCurrentQ(i);
        s.curGainDb[i] = net.getPeakCurrentGainDb(i);
        s.curPan[i] = net.getPeakCurrentPan(i);
        s.gate[i] = net.getPeakGate(i);
        s.rt60[i] = net.getPeakEquivalentRt60(i);
        s.dormant[i] = net.isPeakDormant(i);
        s.engineActive[i] = net.isPeakEngineActive(i);
    }
    return s;
}

/// Deterministic, non-trivial stereo drive. No RNG: the boundary arms compare
/// two instances sample-for-sample and a shared generator would hide a desync.
void fillDrive(std::vector<float>& l, std::vector<float>& r, std::size_t offset) {
    for (std::size_t i = 0; i < l.size(); ++i) {
        const auto n = static_cast<float>(offset + i);
        l[i] = 0.25f * std::sin(0.013f * n);
        r[i] = 0.25f * std::sin(0.017f * n + 0.7f);
    }
}

[[nodiscard]] float maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b) {
    float worst = 0.0f;
    for (std::size_t i = 0; i < a.size(); ++i) {
        worst = std::max(worst, std::abs(a[i] - b[i]));
    }
    return worst;
}

[[nodiscard]] float peakAbs(const std::vector<float>& v) {
    float worst = 0.0f;
    for (const float s : v) {
        worst = std::max(worst, std::abs(s));
    }
    return worst;
}

/// RMS in double. The T009 renders are 6.6e4 - 2.4e5 samples long and a float
/// accumulator loses the tail of a sum of squares at that length.
[[nodiscard]] float rmsOf(const std::vector<float>& v) {
    if (v.empty()) return 0.0f;
    double acc = 0.0;
    for (const float s : v) {
        acc += static_cast<double>(s) * static_cast<double>(s);
    }
    return static_cast<float>(std::sqrt(acc / static_cast<double>(v.size())));
}

/// Scanned WITHOUT a per-sample REQUIRE: a 65 536-sample arm would otherwise
/// register 131 072 assertions and dominate the suite's runtime. The caller
/// REQUIREs the single bool.
[[nodiscard]] bool allFinite(const std::vector<float>& v) {
    for (const float s : v) {
        if (!Krate::DSP::detail::isFinite(s)) return false;
    }
    return true;
}

/// Deterministic white noise at an EXACT target RMS, filled independently per
/// channel. Normalising after the draw (rather than trusting the uniform
/// distribution's a/sqrt(3)) is what lets an arm state "-12 dBFS" and mean it.
void fillNoiseAtDbfs(std::vector<float>& v, std::uint32_t seed, float targetDbfs) {
    Krate::DSP::Xorshift32 rng{seed};
    for (float& s : v) {
        s = rng.nextFloat();  // [-1, +1]
    }
    const float measured = rmsOf(v);
    if (measured <= 0.0f) return;
    const float scale = Krate::DSP::dbToGain(targetDbfs) / measured;
    for (float& s : v) {
        s *= scale;
    }
}

/// Put the FR-043 mix smoother into a LIVE 20 ms transition and leave it there.
///
/// WITHOUT this the guard-ladder arms below are VACUOUS while the wet path is
/// still being built: at the FR-016 default mix = 1 the T006 render emits
/// digital silence on both channels, so "max|diff| == 0 against the reference
/// continuation" would compare zeros against zeros and pass against a build
/// that advanced state it must not touch. Retargeting to 0 makes the output
/// (1 - m) * dry, which varies per sample AND depends on exactly how many
/// samples the instance has consumed - so a rejected call that wrongly advanced
/// the smoother desynchronises the continuation and the arm goes red. 20 ms is
/// 960 samples at 48 kHz, comfortably past the 512 these arms render.
void primeMixTransition(ResonanceDriftNetwork& net) {
    net.setMix(0.0f);
}

} // namespace

// =============================================================================
// T006 arm set - the control-surface contract (FR-008, FR-016, FR-037, FR-050)
// =============================================================================

TEST_CASE("ResonanceDriftNetwork_ControlSurfaceClamps", "[resonance_drift_network]") {
    using AnchorMode = ResonanceDriftNetwork::AnchorMode;
    constexpr std::size_t kMaxPeaks = ResonanceDriftNetwork::kMaxPeaks;

    SECTION("(i) setNumPeaks clamps to [1, kMaxPeaks]") {
        ResonanceDriftNetwork net;
        net.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});

        net.setNumPeaks(0);
        REQUIRE(net.getNumPeaks() == 1u);
        net.setNumPeaks(99);
        REQUIRE(net.getNumPeaks() == kMaxPeaks);
        // FR-019's N is numPeaks, never the awake count (D-11).
        REQUIRE(net.getNormalisationPeakCount() == kMaxPeaks);
    }

    SECTION("(ii) every clamped setter, driven past both ends and read back") {
        ResonanceDriftNetwork net;
        net.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});

        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            net.setPeakQ(i, 400.0f);
            REQUIRE(net.getPeakQ(i) == Catch::Approx(100.0f));
            net.setPeakQ(i, 0.0f);
            REQUIRE(net.getPeakQ(i) == Catch::Approx(0.1f));

            net.setPeakLevel(i, 99.0f);
            REQUIRE(net.getPeakLevel(i) == Catch::Approx(12.0f));
            net.setPeakLevel(i, -99.0f);
            REQUIRE(net.getPeakLevel(i) == Catch::Approx(-60.0f));

            net.setPeakRatio(i, 0.0f);
            REQUIRE(net.getPeakRatio(i) == Catch::Approx(0.25f));
            net.setPeakRatio(i, 999.0f);
            REQUIRE(net.getPeakRatio(i) == Catch::Approx(64.0f));

            net.setPeakPan(i, 9.0f);
            REQUIRE(net.getPeakPan(i) == Catch::Approx(1.0f));
            net.setPeakPan(i, -9.0f);
            REQUIRE(net.getPeakPan(i) == Catch::Approx(-1.0f));

            net.setFreqWander(i, 99.0f);
            REQUIRE(net.getFreqWander(i) == Catch::Approx(24.0f));
            net.setQWander(i, 99.0f);
            REQUIRE(net.getQWander(i) == Catch::Approx(2.0f));
            net.setGainWander(i, 99.0f);
            REQUIRE(net.getGainWander(i) == Catch::Approx(24.0f));

            // The four LOAD-BEARING clamps the first draft of the plan omitted.
            // An unclamped wake scales one peak past unity into the FR-045 wet
            // trim (or, negative, inverts its polarity against the other
            // eleven) and NO other criterion in this phase looks at it.
            net.setPeakWake(i, 9.0f);
            REQUIRE(net.getPeakWakeAmount(i) == Catch::Approx(1.0f));
            net.setPeakWake(i, -1.0f);
            REQUIRE(net.getPeakWakeAmount(i) == Catch::Approx(0.0f));

            net.setPeakPanWander(i, 9.0f);
            REQUIRE(net.getPeakPanWander(i) == Catch::Approx(1.0f));
            net.setPeakPanWander(i, -1.0f);
            REQUIRE(net.getPeakPanWander(i) == Catch::Approx(0.0f));
        }

        net.setWanderRate(5.0f);
        REQUIRE(net.getWanderRate() == Catch::Approx(1.0f));
        net.setWanderRate(0.0f);
        REQUIRE(net.getWanderRate() == Catch::Approx(0.002f));

        net.setSlewCeilings(99.0f, 0.0f);
        REQUIRE(net.getFreqSlewCeiling() == Catch::Approx(24.0f));
        REQUIRE(net.getQSlewCeiling() == Catch::Approx(0.001f));

        net.setGravity(9.0f);
        REQUIRE(net.getGravity() == Catch::Approx(1.0f));
        net.setGravity(-9.0f);
        REQUIRE(net.getGravity() == Catch::Approx(-1.0f));

        net.setNoteFrequency(2.0f);
        REQUIRE(net.getNoteFrequency() == Catch::Approx(8.0f));

        net.setWetGain(99.0f);
        REQUIRE(net.getWetGain() == Catch::Approx(48.0f));
        net.setWetGain(-99.0f);
        REQUIRE(net.getWetGain() == Catch::Approx(-24.0f));

        net.setMix(9.0f);
        REQUIRE(net.getMix() == Catch::Approx(1.0f));
        net.setMix(-9.0f);
        REQUIRE(net.getMix() == Catch::Approx(0.0f));
    }

    SECTION("(iii) FR-037 lane-decimation mapping, asserted directly") {
        ResonanceDriftNetwork net;
        net.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});

        net.setWanderRate(1.0f);
        REQUIRE(net.getLaneDecimation() == 1u);
        net.setWanderRate(0.03f);
        REQUIRE(net.getLaneDecimation() == 2u);
        net.setWanderRate(0.005f);
        REQUIRE(net.getLaneDecimation() == 7u);
        net.setWanderRate(0.002f);
        REQUIRE(net.getLaneDecimation() == 17u);
    }

    SECTION("(iv) out-of-range peak index: silent no-op setters, neutral getters") {
        ResonanceDriftNetwork net;
        net.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});

        const SurfaceSnapshot before = snapshot(net);

        constexpr std::size_t kBad = kMaxPeaks;  // exactly one past the end
        net.setPeakAnchorHz(kBad, 123.0f);
        net.setPeakRatio(kBad, 3.0f);
        net.setPeakLevel(kBad, 0.0f);
        net.setPeakQ(kBad, 50.0f);
        net.setFreqWander(kBad, 12.0f);
        net.setQWander(kBad, 1.0f);
        net.setGainWander(kBad, 12.0f);
        net.setPeakPan(kBad, 0.5f);
        net.setPeakPanWander(kBad, 0.5f);
        net.setPeakWake(kBad, 0.0f);
        net.setPeakDormant(kBad, true);

        const SurfaceSnapshot after = snapshot(net);
        REQUIRE(after == before);

        // Documented neutrals, never an array read past the end.
        REQUIRE(net.getPeakAnchorHz(kBad) == 0.0f);
        REQUIRE(net.getPeakRatio(kBad) == 0.0f);
        REQUIRE(net.getPeakLevel(kBad) == 0.0f);
        REQUIRE(net.getPeakQ(kBad) == 0.0f);
        REQUIRE(net.getFreqWander(kBad) == 0.0f);
        REQUIRE(net.getQWander(kBad) == 0.0f);
        REQUIRE(net.getGainWander(kBad) == 0.0f);
        REQUIRE(net.getPeakPan(kBad) == 0.0f);
        REQUIRE(net.getPeakPanWander(kBad) == 0.0f);
        REQUIRE(net.getPeakWakeAmount(kBad) == 0.0f);
        REQUIRE(net.getPeakCurrentFrequency(kBad) == 0.0f);
        REQUIRE(net.getPeakCurrentQ(kBad) == 0.0f);
        REQUIRE(net.getPeakCurrentGainDb(kBad) == 0.0f);
        REQUIRE(net.getPeakCurrentPan(kBad) == 0.0f);
        REQUIRE(net.getPeakGate(kBad) == 0.0f);
        REQUIRE(net.getPeakEquivalentRt60(kBad) == 0.0f);
        REQUIRE(net.isPeakDormant(kBad) == false);
        REQUIRE(net.isPeakEngineActive(kBad) == false);
        // The mode has no indexed form; its neutral is the default it still reads.
        REQUIRE(net.getAnchorMode() == AnchorMode::Free);
    }

    SECTION("(v) C-9: the sample-rate floor keeps every derived clamp ORDERED") {
        // At a 1 Hz floor the pair [kMinResonatorFrequency, 0.45*fs] = [20, 0.45]
        // is INVERTED, and std::clamp with hi < lo is undefined behaviour -
        // MSVC's <algorithm> fires _STL_VERIFY("invalid bounds argument passed
        // to std::clamp"). The floor is kMinUsableSampleRate = 8000 Hz, which
        // gives [20, 3600].
        constexpr float kLo = Krate::DSP::kMinResonatorFrequency;
        const float kHi = Krate::DSP::kMaxResonatorFrequencyRatio
                          * static_cast<float>(ResonanceDriftNetwork::kMinUsableSampleRate);
        REQUIRE(kHi == Catch::Approx(3600.0f));

        for (const double requested : {0.0, 1.0}) {
            CAPTURE(requested);
            ResonanceDriftNetwork net;
            net.prepare(requested, ResonanceDriftNetwork::PrepareConfig{.maxBlockSamples = 64});

            net.setPeakAnchorHz(0, 10.0f);      // below the 20 Hz floor
            net.setPeakAnchorHz(1, 1.0e6f);     // far above 0.45 * fs
            // Let the dry path through, so the finiteness sweep below is looking
            // at real samples rather than at the all-zero wet path T006 emits.
            primeMixTransition(net);

            std::vector<float> inL(64, 0.0f);
            std::vector<float> inR(64, 0.0f);
            std::vector<float> outL(64, 0.0f);
            std::vector<float> outR(64, 0.0f);
            fillDrive(inL, inR, 0);
            net.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), 64);

            REQUIRE(peakAbs(outL) > 0.0f);
            REQUIRE(peakAbs(outR) > 0.0f);
            for (std::size_t s = 0; s < 64; ++s) {
                REQUIRE(Krate::DSP::detail::isFinite(outL[s]));
                REQUIRE(Krate::DSP::detail::isFinite(outR[s]));
            }
            for (std::size_t i = 0; i < kMaxPeaks; ++i) {
                const float f = net.getPeakCurrentFrequency(i);
                CAPTURE(i, f);
                REQUIRE(f >= kLo);
                REQUIRE(f <= kHi);
            }
        }
    }

    SECTION("(vi) the log2-Q constants are pinned to the shipped Q bounds") {
        // The entropy_processor.h:82 idiom: std::log2 is NOT constexpr in C++20
        // on Clang, so the constants are built from detail::constexprLn /
        // detail::kLn2 and pinned by a RUNTIME equivalence check here. Without
        // this the constexpr series and std::log2 could drift apart on one
        // toolchain and nothing would notice.
        REQUIRE(ResonanceDriftNetwork::kMinLog2Q
                == Catch::Approx(std::log2(Krate::DSP::kMinResonatorQ)).margin(1e-5));
        REQUIRE(ResonanceDriftNetwork::kMaxLog2Q
                == Catch::Approx(std::log2(Krate::DSP::kMaxResonatorQ)).margin(1e-5));
    }
}

// =============================================================================
// SC-011, re-derived by spec correction C-1: the footprint is ZERO
// =============================================================================

TEST_CASE("ResonanceDriftNetwork_PrepareFootprint", "[resonance_drift_network]") {
    // C-1: the declared prepare-time heap footprint is zero bytes and prepare
    // performs zero allocations. maxBlockSamples is retained, clamped and
    // reported, but it SIZES NOTHING in this component - the render is
    // per-sample with local dry capture (plan S5.2), so there is no scratch
    // buffer and no heap term at all. Asserting the RELATIONSHIP (three widely
    // separated block sizes, same answer) is what makes that a real claim
    // rather than one lucky value.
    for (const std::size_t maxBlock : {std::size_t{64}, std::size_t{2048}, std::size_t{8192}}) {
        CAPTURE(maxBlock);
        ResonanceDriftNetwork net;
        const ResonanceDriftNetwork::PrepareConfig cfg{.maxBlockSamples = maxBlock};

        // Warm-up prepare OUTSIDE the scope: any first-touch cost belongs to
        // neither measurement (the atmosphere_engine_test.cpp:2270 idiom).
        net.prepare(kFs, cfg);

        // The count is read from the detector singleton while the scope is
        // still OPEN: AllocationScope latches its own count in its DESTRUCTOR
        // (tests/test_helpers/allocation_detector.h:111-119), so
        // scope.getAllocationCount() would read 0 until the object dies.
        std::size_t allocations = 0;
        {
            [[maybe_unused]] const TestHelpers::AllocationScope scope;
            net.prepare(kFs, cfg);
            allocations = TestHelpers::AllocationDetector::instance().getAllocationCount();
        }

        REQUIRE(allocations == 0u);
        REQUIRE(net.getAllocatedBytes() == 0u);
        // FR-062's second clause: maxBlockSamples is "retained ... and reported
        // by the read surface". All three values above are already inside the
        // [64, 8192] clamp, so each must be echoed unchanged.
        REQUIRE(net.getMaxBlockSamples() == maxBlock);
    }

    // The clamp itself, at both ends - the only way a caller can tell that the
    // figure it handed prepare is not the figure in force.
    {
        ResonanceDriftNetwork net;
        net.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{.maxBlockSamples = 1});
        REQUIRE(net.getMaxBlockSamples() == 64u);
        REQUIRE(net.getAllocatedBytes() == 0u);
        net.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{.maxBlockSamples = 1u << 20u});
        REQUIRE(net.getMaxBlockSamples() == 8192u);
        REQUIRE(net.getAllocatedBytes() == 0u);
        // The default a caller who names nothing gets.
        net.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
        REQUIRE(net.getMaxBlockSamples() == 2048u);
    }
}

// =============================================================================
// SC-005 / FR-061 - zero allocation after prepare, across the WHOLE surface
// =============================================================================
//
// SC-011 (ResonanceDriftNetwork_PrepareFootprint, above) proves prepare itself
// allocates nothing, and SC-019 (e) proves one oversized render does not. This
// is the criterion that covers everything between them, and it is the only one
// that touches the SETTERS: "0 allocations over 20 000 blocks of mixed sizes
// (1, 63, 64, 65, 512, 2048, 4096) that also walk the entire setter surface in
// declaration order (including setWanderRate across values that change FR-037's
// decimation, and setSlewCeilings across its clamps), switch anchor mode in all
// six directions, move setNumPeaks up and down, toggle dormancy on every peak so
// FR-042's sleep-edge reset() + re-apply fires on all twelve, and call reset() -
// all inside an AllocationScope".
//
// Every clause above names a place a later change could reach for the heap and
// no other criterion would notice: a setter that grew a std::vector of anchors,
// a mode switch that built a scratch table, FR-042's sleep edge re-applying a
// configuration through a temporary. The counters below make each clause
// NON-VACUOUS - a walk that never changed the decimation, never fired a sleep
// edge on peak 7 or never actually moved numPeaks would still read zero
// allocations, and would prove nothing.
//
// Nothing inside the scope may allocate for reasons of its own, which rules out
// CAPTURE, REQUIRE and any container growth: every buffer is sized before the
// scope opens and every assertion is made after it closes.
// =============================================================================

TEST_CASE("ResonanceDriftNetwork_NoAllocationAfterPrepare", "[resonance_drift_network]") {
    using AnchorMode = ResonanceDriftNetwork::AnchorMode;

    constexpr std::size_t kBlocks = 20000;
    constexpr std::array<std::size_t, 7> kSizes{1u, 63u, 64u, 65u, 512u, 2048u, 4096u};
    constexpr std::size_t kMaxSize = 4096;
    static_assert(kSizes[6] == kMaxSize, "the buffers below must hold the largest block");

    // The mode walk. Three modes give six ORDERED transitions, and this
    // seven-step cycle is exactly one of each: F->K, K->H, H->F, F->H, H->K,
    // K->F. Anything shorter leaves a direction untested.
    constexpr std::array<AnchorMode, 7> kModeWalk{AnchorMode::Free,   AnchorMode::Keyed,
                                                  AnchorMode::Hybrid, AnchorMode::Free,
                                                  AnchorMode::Hybrid, AnchorMode::Keyed,
                                                  AnchorMode::Free};

    // FR-037's decimation is ceil((1/rate) / BrownianDrift::kTauMax) clamped to
    // [1, 17], so these rates span the mapping end to end (17, 7, 2, 1, 1, 1)
    // and the last two are OUT OF RANGE on purpose, to cross the clamp.
    constexpr std::array<float, 8> kRates{0.002f, 0.005f, 0.03f,   0.1f,
                                          0.5f,   1.0f,   1.0e6f, -5.0f};

    // setSlewCeilings across its clamps: under kMinSlewOctaves, inside, over
    // kMaxSlewOctaves, and back inside.
    constexpr std::array<float, 4> kSlews{0.0f, 0.02f, 1.0e9f, 0.5f};

    // setNumPeaks moved both ways, including past its clamp at both ends.
    constexpr std::array<std::size_t, 8> kPeakCounts{12u, 1u, 7u, 12u, 4u, 12u, 0u, 999u};

    ResonanceDriftNetwork net;
    net.setSeed(0x5C050001u);
    // maxBlockSamples deliberately at its FLOOR while the walk renders blocks
    // sixty-four times larger: FR-062 says the field sizes nothing, and a
    // render that quietly grew a scratch buffer to fit 4 096 samples would show
    // up here as an allocation rather than as a correct-looking render.
    net.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{.maxBlockSamples = 64});
    REQUIRE(net.getMaxBlockSamples() == 64u);

    std::vector<float> inL(kMaxSize);
    std::vector<float> inR(kMaxSize);
    std::vector<float> outL(kMaxSize);
    std::vector<float> outR(kMaxSize);
    fillDrive(inL, inR, 0);

    // ---- non-vacuity evidence, all collected without allocating -------------
    std::size_t allocations = 0;
    std::size_t modeTransitions = 0;
    std::size_t decimationChanges = 0;
    std::size_t numPeaksMoves = 0;
    std::size_t resets = 0;
    std::size_t sleepEdges = 0;
    std::size_t wakeEdges = 0;
    std::array<bool, ResonanceDriftNetwork::kMaxPeaks> sawSleepEdge{};
    std::array<bool, ResonanceDriftNetwork::kMaxPeaks> sawWakeEdge{};
    std::array<bool, ResonanceDriftNetwork::kMaxPeaks> engineWas{};
    std::size_t widestDecimation = 0;
    std::size_t narrowestDecimation = 1000u;
    bool renderedFinite = true;

    {
        [[maybe_unused]] const TestHelpers::AllocationScope scope;

        AnchorMode previousMode = net.getAnchorMode();
        std::size_t previousDecimation = net.getLaneDecimation();
        std::size_t previousNumPeaks = net.getNumPeaks();
        for (std::size_t i = 0; i < ResonanceDriftNetwork::kMaxPeaks; ++i) {
            engineWas[i] = net.isPeakEngineActive(i);
        }

        for (std::size_t b = 0; b < kBlocks; ++b) {
            const std::size_t n = kSizes[b % kSizes.size()];
            const auto fb = static_cast<float>(b);

            // ---- the setter surface, in DECLARATION ORDER -------------------
            net.setNumPeaks(kPeakCounts[b % kPeakCounts.size()]);
            net.setAnchorMode(kModeWalk[b % kModeWalk.size()]);
            for (std::size_t i = 0; i < ResonanceDriftNetwork::kMaxPeaks; ++i) {
                const auto fi = static_cast<float>(i);
                net.setPeakAnchorHz(i, 30.0f + fi * 97.0f + std::fmod(fb, 13.0f));
                net.setPeakRatio(i, 0.5f + fi * 0.37f);
            }
            net.setNoteFrequency(40.0f + std::fmod(fb, 80.0f));
            net.setGravity(std::fmod(fb * 0.017f, 1.0f));
            for (std::size_t i = 0; i < ResonanceDriftNetwork::kMaxPeaks; ++i) {
                const auto fi = static_cast<float>(i);
                net.setPeakLevel(i, -24.0f + std::fmod(fb + fi, 30.0f));
                net.setPeakQ(i, 0.5f + std::fmod(fb + fi * 7.0f, 120.0f));
                net.setFreqWander(i, std::fmod(fb + fi, 26.0f));
                net.setQWander(i, std::fmod(fb * 0.13f + fi, 3.0f));
                net.setGainWander(i, std::fmod(fb * 0.7f + fi, 26.0f));
                net.setPeakPan(i, -1.2f + std::fmod(fb * 0.011f + fi * 0.2f, 2.4f));
                net.setPeakPanWander(i, std::fmod(fb * 0.03f + fi * 0.1f, 1.2f));
            }
            net.setWanderRate(kRates[b % kRates.size()]);
            net.setWanderEnabled((b % 3u) != 0u);
            net.setSlewCeilings(kSlews[b % kSlews.size()], kSlews[(b + 2u) % kSlews.size()]);

            // ---- FR-042's sleep edge, walked across all twelve peaks --------
            // One peak per block, its state flipped every twelfth block, so
            // each peak sees both edge kinds many times over the walk and the
            // reset() + re-apply inside the component runs on every slot.
            const std::size_t dormantPeak = b % ResonanceDriftNetwork::kMaxPeaks;
            const bool dormant = ((b / ResonanceDriftNetwork::kMaxPeaks) % 2u) == 0u;
            net.setPeakWake(dormantPeak, dormant ? 0.0f : 1.0f);
            net.setPeakDormant(dormantPeak, dormant);
            net.setMix(std::fmod(fb * 0.019f, 1.0f));
            net.setWetGain(-30.0f + std::fmod(fb, 80.0f));

            // ---- the lifecycle calls the criterion names --------------------
            if ((b % 997u) == 996u) {
                net.reset();
                ++resets;
            }
            if ((b % 1499u) == 1498u) {
                net.clearAudioState();
            }
            if ((b % 2503u) == 2502u) {
                net.setSeed(0x5C050001u + static_cast<std::uint32_t>(b));
            }

            net.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), n);

            for (std::size_t k = 0; k < n; ++k) {
                if (!Krate::DSP::detail::isFinite(outL[k])
                    || !Krate::DSP::detail::isFinite(outR[k])) {
                    renderedFinite = false;
                }
            }

            // ---- the evidence, read back through the public surface ---------
            const AnchorMode mode = net.getAnchorMode();
            if (mode != previousMode) {
                ++modeTransitions;
                previousMode = mode;
            }
            const std::size_t decimation = net.getLaneDecimation();
            if (decimation != previousDecimation) {
                ++decimationChanges;
                previousDecimation = decimation;
            }
            widestDecimation = std::max(widestDecimation, decimation);
            narrowestDecimation = std::min(narrowestDecimation, decimation);
            const std::size_t peaks = net.getNumPeaks();
            if (peaks != previousNumPeaks) {
                ++numPeaksMoves;
                previousNumPeaks = peaks;
            }
            for (std::size_t i = 0; i < ResonanceDriftNetwork::kMaxPeaks; ++i) {
                const bool active = net.isPeakEngineActive(i);
                if (active != engineWas[i]) {
                    if (active) {
                        ++wakeEdges;
                        sawWakeEdge[i] = true;
                    } else {
                        ++sleepEdges;
                        sawSleepEdge[i] = true;
                    }
                    engineWas[i] = active;
                }
            }
        }

        // Read while the scope is still OPEN: AllocationScope latches its own
        // count in its destructor (allocation_detector.h:111-119).
        allocations = TestHelpers::AllocationDetector::instance().getAllocationCount();
    }

    // ---- the criterion ------------------------------------------------------
    CAPTURE(allocations, modeTransitions, decimationChanges, numPeaksMoves, resets, sleepEdges,
            wakeEdges, widestDecimation, narrowestDecimation);
    REQUIRE(allocations == 0u);

    // ---- and the proof the walk actually walked -----------------------------
    REQUIRE(renderedFinite);
    REQUIRE(modeTransitions >= 6u);
    // FR-037's mapping was crossed in both directions, from the 17-step
    // decimation of the 0.002 Hz floor to the un-decimated 1.
    REQUIRE(widestDecimation == ResonanceDriftNetwork::kMaxLaneDecimation);
    REQUIRE(narrowestDecimation == 1u);
    REQUIRE(decimationChanges >= 6u);
    REQUIRE(numPeaksMoves >= 6u);
    REQUIRE(resets >= 20u);
    // Every one of the twelve slots saw FR-042's sleep edge AND its wake edge.
    for (std::size_t i = 0; i < ResonanceDriftNetwork::kMaxPeaks; ++i) {
        CAPTURE(i);
        REQUIRE(sawSleepEdge[i]);
        REQUIRE(sawWakeEdge[i]);
    }
    REQUIRE(sleepEdges >= ResonanceDriftNetwork::kMaxPeaks);
    REQUIRE(wakeEdges >= ResonanceDriftNetwork::kMaxPeaks);

    // The surface is still live afterwards - an "allocation-free" walk that had
    // wedged the component would otherwise pass.
    REQUIRE(net.isPrepared());
    REQUIRE(net.getMaxBlockSamples() == 64u);
    REQUIRE(net.getAllocatedBytes() == 0u);
}

// =============================================================================
// The render-path guard ladder (FR-003, FR-007). Arms (a) and (e) belong to T009.
// =============================================================================

TEST_CASE("ResonanceDriftNetwork_RenderPathBoundaries", "[resonance_drift_network]") {
    constexpr std::size_t kN = 256;

    SECTION("(a) in-place and cross-aliased renders equal the out-of-place render exactly") {
        // T009. FR-003 admits BOTH aliasings, and plan S5.2 says why they are
        // safe with no scratch buffer: index s of both inputs is read before
        // either output is written. The cross-aliased pairing is the one that
        // catches a render tail which captured dryR only after writing outR.
        constexpr std::uint32_t kSeed = 0x1A11A5EDu;

        std::vector<float> inL(kN);
        std::vector<float> inR(kN);
        fillDrive(inL, inR, 0);

        // The reference: four distinct buffers, mix = 1 (the FR-016 default) so
        // the comparison runs through the whole FR-044 wet path rather than a
        // dry passthrough that would alias trivially.
        ResonanceDriftNetwork ref;
        ref.setSeed(kSeed);
        ref.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
        std::vector<float> refL(kN, 0.0f);
        std::vector<float> refR(kN, 0.0f);
        ref.processBlock(inL.data(), inR.data(), refL.data(), refR.data(), kN);

        // Non-vacuity: an all-zero reference would let every aliasing below
        // pass against a component that wrote nothing at all.
        REQUIRE(peakAbs(refL) > 0.0f);
        REQUIRE(peakAbs(refR) > 0.0f);

        {
            // inL == outL, inR == outR.
            ResonanceDriftNetwork sut;
            sut.setSeed(kSeed);
            sut.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
            std::vector<float> a = inL;
            std::vector<float> b = inR;
            sut.processBlock(a.data(), b.data(), a.data(), b.data(), kN);
            REQUIRE(maxAbsDiff(a, refL) == 0.0f);
            REQUIRE(maxAbsDiff(b, refR) == 0.0f);
        }

        {
            // inL == outR, inR == outL - the cross-aliased pairing.
            ResonanceDriftNetwork sut;
            sut.setSeed(kSeed);
            sut.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
            std::vector<float> a = inL;  // serves as inL and as outR
            std::vector<float> b = inR;  // serves as inR and as outL
            sut.processBlock(a.data(), b.data(), b.data(), a.data(), kN);
            REQUIRE(maxAbsDiff(b, refL) == 0.0f);  // b received outL
            REQUIRE(maxAbsDiff(a, refR) == 0.0f);  // a received outR
        }
    }

    SECTION("(b) each of the four pointers null independently: nothing written, nothing advanced") {
        for (int nullIndex = 0; nullIndex < 4; ++nullIndex) {
            CAPTURE(nullIndex);

            ResonanceDriftNetwork ref;
            ResonanceDriftNetwork sut;
            ref.setSeed(0x51EED10Du);
            sut.setSeed(0x51EED10Du);
            ref.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
            sut.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
            primeMixTransition(ref);
            primeMixTransition(sut);

            std::vector<float> inL(kN);
            std::vector<float> inR(kN);
            fillDrive(inL, inR, 0);

            std::vector<float> refOut1L(kN, 0.0f);
            std::vector<float> refOut1R(kN, 0.0f);
            std::vector<float> sutOut1L(kN, 0.0f);
            std::vector<float> sutOut1R(kN, 0.0f);
            ref.processBlock(inL.data(), inR.data(), refOut1L.data(), refOut1R.data(), kN);
            sut.processBlock(inL.data(), inR.data(), sutOut1L.data(), sutOut1R.data(), kN);

            // The rejected call. Its output buffers are pre-poisoned so a write
            // that should not happen is visible.
            std::vector<float> poisonL(kN, 7.0f);
            std::vector<float> poisonR(kN, 7.0f);
            sut.processBlock(nullIndex == 0 ? nullptr : inL.data(),
                             nullIndex == 1 ? nullptr : inR.data(),
                             nullIndex == 2 ? nullptr : poisonL.data(),
                             nullIndex == 3 ? nullptr : poisonR.data(),
                             kN);
            for (std::size_t s = 0; s < kN; ++s) {
                REQUIRE(poisonL[s] == 7.0f);
                REQUIRE(poisonR[s] == 7.0f);
            }

            // The continuation must be identical: the rejected call advanced
            // NOTHING - not the control phase, not a ramp, not a lane.
            std::vector<float> inL2(kN);
            std::vector<float> inR2(kN);
            fillDrive(inL2, inR2, kN);
            std::vector<float> refOut2L(kN, 0.0f);
            std::vector<float> refOut2R(kN, 0.0f);
            std::vector<float> sutOut2L(kN, 0.0f);
            std::vector<float> sutOut2R(kN, 0.0f);
            ref.processBlock(inL2.data(), inR2.data(), refOut2L.data(), refOut2R.data(), kN);
            sut.processBlock(inL2.data(), inR2.data(), sutOut2L.data(), sutOut2R.data(), kN);

            // Non-vacuity: the comparison below is only a real assertion if the
            // reference actually produced signal.
            REQUIRE(peakAbs(refOut2L) > 0.0f);
            REQUIRE(peakAbs(refOut2R) > 0.0f);
            REQUIRE(maxAbsDiff(refOut2L, sutOut2L) == 0.0f);
            REQUIRE(maxAbsDiff(refOut2R, sutOut2R) == 0.0f);
        }
    }

    SECTION("(c) numSamples == 0 is a no-op consuming no control step") {
        ResonanceDriftNetwork ref;
        ResonanceDriftNetwork sut;
        ref.setSeed(0x0C0FFEE1u);
        sut.setSeed(0x0C0FFEE1u);
        ref.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
        sut.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
        primeMixTransition(ref);
        primeMixTransition(sut);

        std::vector<float> inL(kN);
        std::vector<float> inR(kN);
        fillDrive(inL, inR, 0);

        // 1000 zero-length calls. A single one-sample drift in the ABSOLUTE
        // 64-sample control grid would show up as a whole control step here.
        std::vector<float> scratchL(kN, 0.0f);
        std::vector<float> scratchR(kN, 0.0f);
        for (int i = 0; i < 1000; ++i) {
            sut.processBlock(inL.data(), inR.data(), scratchL.data(), scratchR.data(), 0);
        }
        for (std::size_t s = 0; s < kN; ++s) {
            REQUIRE(scratchL[s] == 0.0f);
            REQUIRE(scratchR[s] == 0.0f);
        }

        std::vector<float> refOutL(kN, 0.0f);
        std::vector<float> refOutR(kN, 0.0f);
        std::vector<float> sutOutL(kN, 0.0f);
        std::vector<float> sutOutR(kN, 0.0f);
        ref.processBlock(inL.data(), inR.data(), refOutL.data(), refOutR.data(), kN);
        sut.processBlock(inL.data(), inR.data(), sutOutL.data(), sutOutR.data(), kN);

        REQUIRE(peakAbs(refOutL) > 0.0f);
        REQUIRE(peakAbs(refOutR) > 0.0f);
        REQUIRE(maxAbsDiff(refOutL, sutOutL) == 0.0f);
        REQUIRE(maxAbsDiff(refOutR, sutOutR) == 0.0f);
    }

    SECTION("(d) processBlock before prepare writes exactly numSamples zeros and advances nothing") {
        ResonanceDriftNetwork sut;
        sut.setSeed(0xBEEFCAFEu);
        REQUIRE_FALSE(sut.isPrepared());

        constexpr std::size_t kGuard = 8;
        std::vector<float> inL(kN);
        std::vector<float> inR(kN);
        fillDrive(inL, inR, 0);
        std::vector<float> outL(kN + kGuard, 7.0f);
        std::vector<float> outR(kN + kGuard, 7.0f);

        sut.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), kN);

        for (std::size_t s = 0; s < kN; ++s) {
            REQUIRE(outL[s] == 0.0f);
            REQUIRE(outR[s] == 0.0f);
        }
        for (std::size_t s = kN; s < kN + kGuard; ++s) {
            REQUIRE(outL[s] == 7.0f);  // exactly numSamples zeros, not one more
            REQUIRE(outR[s] == 7.0f);
        }

        // ...and nothing advanced: a twin that never made the un-prepared call
        // renders identically from here on.
        ResonanceDriftNetwork ref;
        ref.setSeed(0xBEEFCAFEu);
        ref.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
        sut.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
        primeMixTransition(ref);
        primeMixTransition(sut);

        std::vector<float> refOutL(kN, 0.0f);
        std::vector<float> refOutR(kN, 0.0f);
        std::vector<float> sutOutL(kN, 0.0f);
        std::vector<float> sutOutR(kN, 0.0f);
        ref.processBlock(inL.data(), inR.data(), refOutL.data(), refOutR.data(), kN);
        sut.processBlock(inL.data(), inR.data(), sutOutL.data(), sutOutR.data(), kN);

        REQUIRE(peakAbs(refOutL) > 0.0f);
        REQUIRE(peakAbs(refOutR) > 0.0f);
        REQUIRE(maxAbsDiff(refOutL, sutOutL) == 0.0f);
        REQUIRE(maxAbsDiff(refOutR, sutOutR) == 0.0f);
    }

    SECTION("(e) numSamples far above maxBlockSamples renders correctly and allocates nothing") {
        // T009. maxBlockSamples SIZES NOTHING (spec correction C-1), so a
        // 65 536-sample call against a config that declared 64 must render
        // exactly as the same content rendered in 512-sample blocks - and must
        // not reach for a scratch buffer to do it.
        constexpr std::size_t kLong = 65536;
        constexpr std::uint32_t kSeed = 0x600D5EEDu;

        std::vector<float> inL(kLong);
        std::vector<float> inR(kLong);
        fillDrive(inL, inR, 0);

        ResonanceDriftNetwork ref;
        ref.setSeed(kSeed);
        ref.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
        std::vector<float> refL(kLong, 0.0f);
        std::vector<float> refR(kLong, 0.0f);
        for (std::size_t done = 0; done < kLong; done += 512) {
            ref.processBlock(inL.data() + done, inR.data() + done,
                             refL.data() + done, refR.data() + done, 512);
        }
        REQUIRE(peakAbs(refL) > 0.0f);
        REQUIRE(peakAbs(refR) > 0.0f);

        ResonanceDriftNetwork sut;
        sut.setSeed(kSeed);
        sut.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{.maxBlockSamples = 64});
        std::vector<float> outL(kLong, 0.0f);
        std::vector<float> outR(kLong, 0.0f);

        // The count is read while the scope is still OPEN: AllocationScope
        // latches its own count in its DESTRUCTOR (allocation_detector.h:111-119).
        std::size_t allocations = 0;
        {
            [[maybe_unused]] const TestHelpers::AllocationScope scope;
            sut.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), kLong);
            allocations = TestHelpers::AllocationDetector::instance().getAllocationCount();
        }
        REQUIRE(allocations == 0u);

        REQUIRE(allFinite(outL));
        REQUIRE(allFinite(outR));

        const float sutDbL = Krate::DSP::gainToDb(rmsOf(outL));
        const float sutDbR = Krate::DSP::gainToDb(rmsOf(outR));
        const float refDbL = Krate::DSP::gainToDb(rmsOf(refL));
        const float refDbR = Krate::DSP::gainToDb(rmsOf(refR));
        CAPTURE(sutDbL, refDbL, sutDbR, refDbR);
        REQUIRE(std::abs(sutDbL - refDbL) <= 0.5f);
        REQUIRE(std::abs(sutDbR - refDbR) <= 0.5f);
    }
}

// =============================================================================
// T007 - anchors, the three AnchorModes (FR-020..FR-025) and the FR-035 slew
// ceiling. Every arm runs with wander DISABLED, so the only thing that can move
// a peak is the anchor math and the limiter that shapes its approach.
// =============================================================================

namespace {

constexpr std::size_t kControlChunk = ResonanceDriftNetwork::kControlChunkSamples;

/// The settling clause every AnchorModes arm inherits. FR-035 caps a ONE-OCTAVE
/// move at 1 / kDefaultFreqStepOctaves = 50 control steps, and the Keyed
/// downward arm below asks for 1.46 octaves (73 steps), so a read taken before
/// ~75 steps would be measuring the slew ramp rather than the anchor. 200 is
/// that bound with room to spare, and it is deliberately a CONSTANT rather than
/// a per-arm number: an arm that needed more would be describing a bug.
constexpr std::size_t kSettleChunks = 200;

/// Render whole 64-sample control chunks of silence, one control step each.
/// The control path never reads the audio, so silence is the honest drive for
/// these arms: it keeps them measuring the anchor resolution and the limiter
/// rather than the render tail T009 has yet to build.
void advanceControlSteps(ResonanceDriftNetwork& net, std::size_t chunks) {
    std::array<float, kControlChunk> inL{};
    std::array<float, kControlChunk> inR{};
    std::array<float, kControlChunk> outL{};
    std::array<float, kControlChunk> outR{};
    for (std::size_t c = 0; c < chunks; ++c) {
        net.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), kControlChunk);
    }
}

[[nodiscard]] float centsBetween(float a, float b) {
    return 1200.0f * std::log2(a / b);
}

/// The FR-016 default patch with wander off - the fixed starting point for
/// every T007 arm.
void prepareStatic(ResonanceDriftNetwork& net) {
    net.setSeed(0xA0C0DE01u);
    net.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
    net.setWanderEnabled(false);
}

} // namespace

TEST_CASE("ResonanceDriftNetwork_AnchorModes", "[resonance_drift_network]") {
    using AnchorMode = ResonanceDriftNetwork::AnchorMode;
    constexpr std::size_t kMaxPeaks = ResonanceDriftNetwork::kMaxPeaks;

    // The Nyquist-derived clamp pair, computed from the shipped constants rather
    // than transcribed, so the expectations below track a sample-rate change.
    const float kMinLog2Hz = std::log2(Krate::DSP::kMinResonatorFrequency);
    const float kMaxLog2Hz =
        std::log2(Krate::DSP::kMaxResonatorFrequencyRatio * static_cast<float>(kFs));

    SECTION("(a) Free: every peak sits on its own configured anchor") {
        ResonanceDriftNetwork net;
        prepareStatic(net);
        net.setAnchorMode(AnchorMode::Free);
        advanceControlSteps(net, kSettleChunks);

        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            const float anchor = net.getPeakAnchorHz(i);
            const float realised = net.getPeakCurrentFrequency(i);
            CAPTURE(i, anchor, realised);
            REQUIRE(std::abs(centsBetween(realised, anchor)) <= 1.0f);
        }
    }

    SECTION("(b) Keyed: noteHz * ratio, transposition, and the 20 Hz floor") {
        ResonanceDriftNetwork net;
        prepareStatic(net);
        net.setAnchorMode(AnchorMode::Keyed);
        net.setNoteFrequency(55.0f);
        advanceControlSteps(net, kSettleChunks);

        std::array<float, kMaxPeaks> before{};
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            const float expected = 55.0f * net.getPeakRatio(i);
            before[i] = net.getPeakCurrentFrequency(i);
            CAPTURE(i, expected, before[i]);
            REQUIRE(std::abs(centsBetween(before[i], expected)) <= 1.0f);
        }

        // UPWARD octave: nothing clamps anywhere (peak 11 lands at 1320 Hz), so
        // every peak must move by the full 1200 cents.
        net.setNoteFrequency(110.0f);
        advanceControlSteps(net, kSettleChunks);
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            const float moved = centsBetween(net.getPeakCurrentFrequency(i), before[i]);
            CAPTURE(i, moved);
            REQUIRE(moved == Catch::Approx(1200.0f).margin(2.0f));
        }

        // DOWNWARD, chosen as an explicit CLAMP check instead of a second
        // transposition: at noteHz = 27.5 peak 0's keyed anchor is
        // 27.5 * 0.5 = 13.75 Hz, BELOW kMinResonatorFrequency = 20 Hz, so peak 0
        // - and only peak 0, since peak 1's 27.5 Hz already clears the floor -
        // must sit exactly on the floor rather than wherever an unclamped
        // std::exp2 would put it.
        net.setNoteFrequency(27.5f);
        advanceControlSteps(net, kSettleChunks);
        REQUIRE(std::abs(centsBetween(net.getPeakCurrentFrequency(0),
                                      Krate::DSP::kMinResonatorFrequency))
                <= 1.0f);
        for (std::size_t i = 1; i < kMaxPeaks; ++i) {
            const float expected = 27.5f * net.getPeakRatio(i);
            CAPTURE(i, expected);
            REQUIRE(expected > Krate::DSP::kMinResonatorFrequency);  // non-vacuity
            REQUIRE(std::abs(centsBetween(net.getPeakCurrentFrequency(i), expected)) <= 1.0f);
        }
    }

    SECTION("(c) Hybrid at gravity 0 is BIT-EQUAL to Free") {
        ResonanceDriftNetwork net;
        prepareStatic(net);
        net.setAnchorMode(AnchorMode::Free);
        advanceControlSteps(net, kSettleChunks);

        std::array<float, kMaxPeaks> freeHz{};
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            freeHz[i] = net.getPeakCurrentFrequency(i);
        }

        net.setAnchorMode(AnchorMode::Hybrid);
        net.setGravity(0.0f);
        advanceControlSteps(net, kSettleChunks);

        // `==`, not Approx: this passes only with an explicit gravity == 0
        // short-circuit. An implementation that always evaluates
        // freeLog2 + g * (keyedLog2 - freeLog2) may agree here by luck today and
        // stop agreeing the moment the expression is reassociated.
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            CAPTURE(i, freeHz[i]);
            REQUIRE(net.getPeakCurrentFrequency(i) == freeHz[i]);
        }
    }

    SECTION("(d) Hybrid at gravity 1 is each peak OWN keyed anchor, 0.5 the closed form") {
        ResonanceDriftNetwork net;
        prepareStatic(net);
        net.setAnchorMode(AnchorMode::Hybrid);
        net.setNoteFrequency(55.0f);
        net.setGravity(1.0f);
        advanceControlSteps(net, kSettleChunks);

        std::array<float, kMaxPeaks> realised{};
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            const float keyed = 55.0f * net.getPeakRatio(i);
            realised[i] = net.getPeakCurrentFrequency(i);
            CAPTURE(i, keyed, realised[i]);
            REQUIRE(std::abs(centsBetween(realised[i], keyed)) <= 1.0f);
        }

        // The assertion that catches a nearest-neighbour collapse: twelve peaks
        // pulled to a SHARED grid would land on top of each other, and every
        // per-peak check above would still pass for the ones that happened to
        // own their grid point.
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            for (std::size_t j = i + 1; j < kMaxPeaks; ++j) {
                CAPTURE(i, j, realised[i], realised[j]);
                REQUIRE(std::abs(centsBetween(realised[i], realised[j])) > 1.0f);
            }
        }

        // g = 0.5 against FR-023 closed form, in the log domain it is stated in.
        net.setGravity(0.5f);
        advanceControlSteps(net, kSettleChunks);
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            const float freeLog2 = std::log2(net.getPeakAnchorHz(i));
            const float keyedLog2 = std::log2(55.0f * net.getPeakRatio(i));
            const float expected =
                std::exp2(std::clamp(freeLog2 + 0.5f * (keyedLog2 - freeLog2),
                                     kMinLog2Hz, kMaxLog2Hz));
            CAPTURE(i, expected);
            REQUIRE(std::abs(centsBetween(net.getPeakCurrentFrequency(i), expected)) <= 1.0f);
        }
    }

    SECTION("(e) Hybrid at gravity -1 is the per-peak log mirror") {
        ResonanceDriftNetwork net;
        prepareStatic(net);
        net.setAnchorMode(AnchorMode::Hybrid);
        net.setNoteFrequency(55.0f);
        net.setGravity(-1.0f);
        advanceControlSteps(net, kSettleChunks);

        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            const float freeLog2 = std::log2(net.getPeakAnchorHz(i));
            const float keyedLog2 = std::log2(55.0f * net.getPeakRatio(i));
            const float expected = std::exp2(
                std::clamp(2.0f * freeLog2 - keyedLog2, kMinLog2Hz, kMaxLog2Hz));
            CAPTURE(i, expected);
            REQUIRE(std::abs(centsBetween(net.getPeakCurrentFrequency(i), expected)) <= 1.0f);
        }
    }

    SECTION("(f) sweeping gravity -1 -> +1 moves every peak monotonically, within the ceiling") {
        ResonanceDriftNetwork net;
        prepareStatic(net);
        net.setAnchorMode(AnchorMode::Hybrid);
        net.setNoteFrequency(55.0f);
        net.setGravity(-1.0f);
        advanceControlSteps(net, kSettleChunks);

        // The direction each peak MUST move in, derived per peak from the sign of
        // (keyedLog2 - freeLog2): peak 1 ratio is exactly 1.0 against a 55 Hz
        // note and its free anchor is 55 Hz, so its direction is 0 and it must not
        // move at all - a peak that drifts there is a defect the other eleven hide.
        std::array<float, kMaxPeaks> direction{};
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            const float freeLog2 = std::log2(net.getPeakAnchorHz(i));
            const float keyedLog2 = std::log2(55.0f * net.getPeakRatio(i));
            const float delta = keyedLog2 - freeLog2;
            // Written as an if rather than a nested ternary: same three outcomes,
            // and the "exactly zero delta means exactly no motion" case reads as
            // the deliberate one it is.
            float dir = 0.0f;
            if (std::abs(delta) >= 1.0e-6f) {
                dir = (delta > 0.0f) ? 1.0f : -1.0f;
            }
            direction[i] = dir;
        }

        std::array<float, kMaxPeaks> previous{};
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            previous[i] = std::log2(net.getPeakCurrentFrequency(i));
        }

        const float ceiling = net.getFreqSlewCeiling();
        REQUIRE(ceiling == Catch::Approx(0.02f));

        for (int step = 1; step <= 40; ++step) {
            const float g = -1.0f + 0.05f * static_cast<float>(step);
            net.setGravity(g);

            // Settle one control step at a time, so the per-step motion is
            // observable rather than averaged away by a bulk advance. The worst
            // step over every peak and every chunk is reduced to ONE assertion:
            // 96 000 REQUIREs with a five-value CAPTURE apiece would dominate
            // this TU's runtime while proving exactly the same bound.
            float worstStep = 0.0f;
            for (std::size_t c = 0; c < kSettleChunks; ++c) {
                std::array<float, kMaxPeaks> beforeStep{};
                for (std::size_t i = 0; i < kMaxPeaks; ++i) {
                    beforeStep[i] = std::log2(net.getPeakCurrentFrequency(i));
                }
                advanceControlSteps(net, 1);
                for (std::size_t i = 0; i < kMaxPeaks; ++i) {
                    const float now = std::log2(net.getPeakCurrentFrequency(i));
                    worstStep = std::max(worstStep, std::abs(now - beforeStep[i]));
                }
            }
            CAPTURE(step, g, worstStep);
            REQUIRE(worstStep <= ceiling + 1.0e-4f);

            for (std::size_t i = 0; i < kMaxPeaks; ++i) {
                const float now = std::log2(net.getPeakCurrentFrequency(i));
                const float moved = now - previous[i];
                CAPTURE(step, g, i, direction[i], moved);
                REQUIRE(direction[i] * moved >= -1.0e-4f);  // no reversal
                if (direction[i] == 0.0f) {
                    REQUIRE(std::abs(moved) <= 1.0e-4f);
                }
                previous[i] = now;
            }
        }
    }
}

TEST_CASE("ResonanceDriftNetwork_SlewLimit", "[resonance_drift_network]") {
    using AnchorMode = ResonanceDriftNetwork::AnchorMode;
    constexpr std::size_t kMaxPeaks = ResonanceDriftNetwork::kMaxPeaks;

    // "Arrived" for the step counts below. One octave takes 50 steps at the
    // default ceiling, so the remaining distance one step short of arrival is
    // 0.02 octaves = 24 cents - four orders above this, which is what makes the
    // count exact rather than tolerance-dependent.
    constexpr float kArrivedOctaves = 1.0e-3f;

    // The move is a Keyed one-octave transposition: every peak target moves by
    // exactly 1.0 in log2, and nothing clamps (peak 0 lands on 55 Hz, peak 11 on
    // 1320 Hz), so the step counts below are the limiter and nothing else.
    const auto settleKeyed = [](ResonanceDriftNetwork& net) {
        prepareStatic(net);
        net.setAnchorMode(AnchorMode::Keyed);
        net.setNoteFrequency(55.0f);
        advanceControlSteps(net, kSettleChunks);
    };

    SECTION("(a) no control step moves any peak by more than the frequency ceiling") {
        ResonanceDriftNetwork net;
        settleKeyed(net);
        const float ceiling = net.getFreqSlewCeiling();
        REQUIRE(ceiling == Catch::Approx(0.02f));

        std::array<float, kMaxPeaks> previous{};
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            previous[i] = std::log2(net.getPeakCurrentFrequency(i));
        }

        net.setNoteFrequency(110.0f);
        for (int step = 0; step < 120; ++step) {
            advanceControlSteps(net, 1);
            for (std::size_t i = 0; i < kMaxPeaks; ++i) {
                const float now = std::log2(net.getPeakCurrentFrequency(i));
                const float stepped = std::abs(now - previous[i]);
                CAPTURE(step, i, stepped);
                REQUIRE(stepped <= ceiling + 1.0e-4f);
                previous[i] = now;
            }
        }

        // Non-vacuity: the move really happened, it was just rationed.
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            const float expected = 110.0f * net.getPeakRatio(i);
            CAPTURE(i, expected);
            REQUIRE(std::abs(centsBetween(net.getPeakCurrentFrequency(i), expected)) <= 1.0f);
        }
    }

    SECTION("(b) a one-octave move takes 1.0 / ceiling = 50 +/- 2 control steps") {
        ResonanceDriftNetwork net;
        settleKeyed(net);

        std::array<float, kMaxPeaks> targetLog2{};
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            targetLog2[i] = std::log2(110.0f * net.getPeakRatio(i));
        }

        net.setNoteFrequency(110.0f);
        std::array<int, kMaxPeaks> arrivedAt{};
        arrivedAt.fill(-1);
        for (int step = 1; step <= 400; ++step) {
            advanceControlSteps(net, 1);
            for (std::size_t i = 0; i < kMaxPeaks; ++i) {
                if (arrivedAt[i] < 0
                    && std::abs(std::log2(net.getPeakCurrentFrequency(i)) - targetLog2[i])
                           <= kArrivedOctaves) {
                    arrivedAt[i] = step;
                }
            }
        }

        // 50 is 1.0 octave / 0.02 octaves per step. A build with NO limiter
        // arrives in 1 step; one applying the ceiling in the linear domain, or
        // per sample, or per block, lands nowhere near 50 either.
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            CAPTURE(i, arrivedAt[i]);
            REQUIRE(arrivedAt[i] >= 48);
            REQUIRE(arrivedAt[i] <= 52);
        }
    }

    SECTION("(d) setSlewCeilings is a real control surface: 0.005 makes it 200 +/- 5 steps") {
        ResonanceDriftNetwork net;
        settleKeyed(net);
        net.setSlewCeilings(0.005f, 0.005f);
        REQUIRE(net.getFreqSlewCeiling() == Catch::Approx(0.005f));
        REQUIRE(net.getQSlewCeiling() == Catch::Approx(0.005f));

        std::array<float, kMaxPeaks> targetLog2{};
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            targetLog2[i] = std::log2(110.0f * net.getPeakRatio(i));
        }

        net.setNoteFrequency(110.0f);
        std::array<int, kMaxPeaks> arrivedAt{};
        arrivedAt.fill(-1);
        for (int step = 1; step <= 600; ++step) {
            advanceControlSteps(net, 1);
            for (std::size_t i = 0; i < kMaxPeaks; ++i) {
                if (arrivedAt[i] < 0
                    && std::abs(std::log2(net.getPeakCurrentFrequency(i)) - targetLog2[i])
                           <= kArrivedOctaves) {
                    arrivedAt[i] = step;
                }
            }
        }

        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            CAPTURE(i, arrivedAt[i]);
            REQUIRE(arrivedAt[i] >= 195);
            REQUIRE(arrivedAt[i] <= 205);
        }
    }
}

// =============================================================================
// T008 arm set - the wander lanes (FR-030..FR-034, FR-036, FR-037, FR-038)
// =============================================================================
//
// What each arm is FOR, since three of the four would be vacuous if written the
// obvious way:
//   (a) the FR-030/FR-031 excursion bounds, taken at MAXIMUM depth over more
//       than 1e5 samplings - the only arm that would catch a map that added the
//       lane in the wrong domain (linear Hz instead of log2) or dropped a clamp;
//   (b) the property that makes a lane a LANE rather than noise: per-peak
//       persistence at T/8 and decorrelation by 8T, T = 1 / wanderRate;
//   (c) FR-037's decimation mapping, and the REBASE (not restart) of the lane
//       counter that a mid-render rate change must perform;
//   (d) FR-034's "freezes the drift WITHOUT rewinding it".
// =============================================================================

namespace {

constexpr std::size_t kNumPeaks = ResonanceDriftNetwork::kMaxPeaks;

/// The lane properties arm (b) measures are defined in SECONDS - the OU
/// correlation time - and FR-037's decimation is sample-rate independent (the
/// lane advance takes a FIXED kControlChunkSamples argument), so the record may
/// be taken at the lowest rate prepare() accepts. That is a 6x shorter render
/// for a measurement identical in every way that matters, and 8000 / 64 = 125
/// control steps per second exactly, with no rounding in the lag arithmetic.
constexpr double kSlowFs = ResonanceDriftNetwork::kMinUsableSampleRate;
constexpr std::size_t kSlowStepsPerSecond = static_cast<std::size_t>(kSlowFs) / kControlChunk;
static_assert(kSlowStepsPerSecond == 125, "the lag arithmetic below assumes an exact division");

/// log2 of every peak's realised frequency, sampled once per control step.
void recordFreqLog2(ResonanceDriftNetwork& net, std::size_t steps,
                    std::array<std::vector<float>, kNumPeaks>& out) {
    for (std::size_t s = 0; s < steps; ++s) {
        advanceControlSteps(net, 1);
        for (std::size_t i = 0; i < kNumPeaks; ++i) {
            out[i].push_back(std::log2(net.getPeakCurrentFrequency(i)));
        }
    }
}

/// Normalised sample autocorrelation at `lag`, sample mean removed.
///
/// The standard biased estimator: ONE denominator for every lag, so the result
/// is in [-1, +1] by construction and a long lag cannot inflate it. Accumulated
/// in double - the record is 1.5e5 samples of a value near 5.3 (log2 of a few
/// hundred Hz) and a float sum of squares would lose the mean-removed tail.
[[nodiscard]] float autocorrelation(const std::vector<float>& x, std::size_t lag) {
    const std::size_t n = x.size();
    REQUIRE(n > lag * 2);  // otherwise the estimate is meaningless, not merely noisy

    double mean = 0.0;
    for (const float v : x) {
        mean += static_cast<double>(v);
    }
    mean /= static_cast<double>(n);

    double num = 0.0;
    double den = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double d = static_cast<double>(x[i]) - mean;
        den += d * d;
        if (i + lag < n) {
            num += d * (static_cast<double>(x[i + lag]) - mean);
        }
    }
    return (den > 0.0) ? static_cast<float>(num / den) : 0.0f;
}

} // namespace

TEST_CASE("ResonanceDriftNetwork_LaneBounds", "[resonance_drift_network]") {
    SECTION("(a) FR-030/FR-031 bounds hold at maximum depth over >= 1e5 samplings") {
        ResonanceDriftNetwork net;
        net.setSeed(0x1A5E0B08u);
        net.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
        for (std::size_t i = 0; i < kNumPeaks; ++i) {
            // 24 semitones = +/- 2 octaves of frequency; 2 octaves of Q.
            net.setFreqWander(i, ResonanceDriftNetwork::kMaxFreqWanderSemis);
            net.setQWander(i, ResonanceDriftNetwork::kMaxQWanderOctaves);
            net.setPeakQ(i, 12.0f);
        }
        net.setWanderRate(ResonanceDriftNetwork::kMaxWanderRateHz);  // 1 Hz: the fastest lane
        REQUIRE(net.isWanderEnabled());
        REQUIRE(net.getLaneDecimation() == 1u);

        // The bounds are DERIVED from the shipped constants, never transcribed:
        // FR-030 admits +/- depth/12 octaves around the anchor, intersected with
        // the [20 Hz, 0.45 * fs] pair the map clamps to; FR-031 admits
        // +/- depth octaves around the base Q, intersected with ResonatorBank's
        // own [0.1, 100].
        const float nyquistCap =
            Krate::DSP::kMaxResonatorFrequencyRatio * static_cast<float>(kFs);
        std::array<float, kNumPeaks> loHz{};
        std::array<float, kNumPeaks> hiHz{};
        for (std::size_t i = 0; i < kNumPeaks; ++i) {
            const float anchor = net.getPeakAnchorHz(i);
            loHz[i] = std::max(Krate::DSP::kMinResonatorFrequency, anchor * 0.25f);
            hiHz[i] = std::min(nyquistCap, anchor * 4.0f);
        }
        const float loQ = std::max(Krate::DSP::kMinResonatorQ, 12.0f * 0.25f);  // 3
        const float hiQ = std::min(Krate::DSP::kMaxResonatorQ, 12.0f * 4.0f);   // 48

        // 12 s at 48 kHz = 9000 control steps; 9000 * 12 peaks * 2 quantities =
        // 216 000 samplings, comfortably past the 1e5 the criterion asks for.
        // The extremes are accumulated rather than asserted per sampling: a
        // REQUIRE per sampling would be 216 000 Catch2 assertions for exactly
        // the same statement, and the extremes ARE that statement.
        constexpr std::size_t kSteps = 9000;
        std::array<float, kNumPeaks> minHz{};
        std::array<float, kNumPeaks> maxHz{};
        std::array<float, kNumPeaks> minQ{};
        std::array<float, kNumPeaks> maxQ{};
        for (std::size_t i = 0; i < kNumPeaks; ++i) {
            minHz[i] = net.getPeakCurrentFrequency(i);
            maxHz[i] = minHz[i];
            minQ[i] = net.getPeakCurrentQ(i);
            maxQ[i] = minQ[i];
        }

        bool allFinite = true;
        for (std::size_t s = 0; s < kSteps; ++s) {
            advanceControlSteps(net, 1);
            for (std::size_t i = 0; i < kNumPeaks; ++i) {
                const float f = net.getPeakCurrentFrequency(i);
                const float q = net.getPeakCurrentQ(i);
                allFinite = allFinite && Krate::DSP::detail::isFinite(f)
                            && Krate::DSP::detail::isFinite(q);
                minHz[i] = std::min(minHz[i], f);
                maxHz[i] = std::max(maxHz[i], f);
                minQ[i] = std::min(minQ[i], q);
                maxQ[i] = std::max(maxQ[i], q);
            }
        }
        REQUIRE(allFinite);

        // 1e-4 relative is 0.17 cents: float rounding on an exp2 of a clamped
        // log2, and nothing else.
        constexpr float kRel = 1.0e-4f;
        for (std::size_t i = 0; i < kNumPeaks; ++i) {
            CAPTURE(i, minHz[i], maxHz[i], loHz[i], hiHz[i], minQ[i], maxQ[i]);
            REQUIRE(minHz[i] >= loHz[i] * (1.0f - kRel));
            REQUIRE(maxHz[i] <= hiHz[i] * (1.0f + kRel));
            REQUIRE(minQ[i] >= loQ * (1.0f - kRel));
            REQUIRE(maxQ[i] <= hiQ * (1.0f + kRel));
        }

        // NON-VACUITY. Every bound above passes on a build whose lanes never
        // advance at all, so the excursion has to be shown to be real: at least
        // eight of the twelve peaks must have covered a quarter of an octave of
        // frequency and a quarter of an octave of Q in those twelve seconds.
        int movedHz = 0;
        int movedQ = 0;
        for (std::size_t i = 0; i < kNumPeaks; ++i) {
            if (std::log2(maxHz[i] / minHz[i]) > 0.25f) {
                ++movedHz;
            }
            if (std::log2(maxQ[i] / minQ[i]) > 0.25f) {
                ++movedQ;
            }
        }
        CAPTURE(movedHz, movedQ);
        REQUIRE(movedHz >= 8);
        REQUIRE(movedQ >= 8);
    }

    SECTION("(b) each peak's own lane persists at T/8 and has decorrelated by 8T") {
        // T = 1 / wanderRate = 1 s here, the shortest correlation time the
        // control surface admits, which is what keeps the record finite: the
        // sample autocorrelation at a lag well past T has standard error
        // ~ sqrt(T / recordLength), so 1200 s of record puts the 0.10 bound
        // 3.4 sigma away for all twelve peaks at once. A record of a few T - the
        // length every other arm uses - would be measuring the estimator.
        ResonanceDriftNetwork net;
        net.setSeed(0x5EED0B08u);
        net.prepare(kSlowFs, ResonanceDriftNetwork::PrepareConfig{});

        // The ceiling is lifted out of the way so this arm measures the LANE and
        // not the FR-035 limiter, and +/- 6 semitones keeps every peak clear of
        // the [20 Hz, 0.45 * fs] clamp - saturation against it would inflate the
        // measured persistence of the lowest peaks and hide a stalled lane.
        net.setSlewCeilings(ResonanceDriftNetwork::kMaxSlewOctaves,
                            ResonanceDriftNetwork::kMaxSlewOctaves);
        for (std::size_t i = 0; i < kNumPeaks; ++i) {
            net.setFreqWander(i, 6.0f);
        }
        net.setWanderRate(ResonanceDriftNetwork::kMaxWanderRateHz);
        REQUIRE(net.getLaneDecimation() == 1u);

        constexpr std::size_t kRecordSeconds = 1200;
        constexpr std::size_t kSteps = kRecordSeconds * kSlowStepsPerSecond;  // 150 000
        constexpr std::size_t kLagShort = kSlowStepsPerSecond / 8;            // T/8
        constexpr std::size_t kLagLong = 8 * kSlowStepsPerSecond;             // 8T

        std::array<std::vector<float>, kNumPeaks> traj;
        for (auto& v : traj) {
            v.reserve(kSteps);
        }
        recordFreqLog2(net, kSteps, traj);

        for (std::size_t i = 0; i < kNumPeaks; ++i) {
            const float rShort = autocorrelation(traj[i], kLagShort);
            const float rLong = autocorrelation(traj[i], kLagLong);
            CAPTURE(i, rShort, rLong);
            // exp(-1/8) = 0.88 for a pure OU at this lag, so 0.20 is a floor a
            // per-step white-noise "lane" cannot clear.
            REQUIRE(rShort >= 0.20f);
            // exp(-8) = 3e-4 for a pure OU. A lane whose realised correlation
            // time is far longer than 1 / wanderRate - FR-037's dead zone seen
            // from the other side - is red here.
            REQUIRE(rLong <= 0.10f);
        }

        // NON-VACUITY: a lane pinned at a constant has an undefined
        // autocorrelation that the helper reports as 0, which would pass the
        // lag-8T bound. Every peak must have actually moved - 0.1 octaves is
        // 120 cents against a depth of +/- 600.
        for (std::size_t i = 0; i < kNumPeaks; ++i) {
            const auto extremes = std::minmax_element(traj[i].begin(), traj[i].end());
            const float spanOctaves = *extremes.second - *extremes.first;
            CAPTURE(i, spanOctaves);
            REQUIRE(spanOctaves > 0.10f);
        }
    }

    SECTION("(c) the FR-037 decimation mapping, and a rate change that neither "
            "forces nor skips a lane advance") {
        // (c-1) The mapping itself, at the four worked rates (plan S6.2).
        {
            ResonanceDriftNetwork net;
            net.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});

            net.setWanderRate(1.0f);  // T = 1 s, tau = 1 s
            REQUIRE(net.getLaneDecimation() == 1u);
            net.setWanderRate(0.03f);  // T = 33.3 s, past kTauMax
            REQUIRE(net.getLaneDecimation() == 2u);
            net.setWanderRate(0.005f);  // T = 200 s
            REQUIRE(net.getLaneDecimation() == 7u);
            net.setWanderRate(0.002f);  // T = 500 s, the floor
            REQUIRE(net.getLaneDecimation() == 17u);
            REQUIRE(net.getLaneDecimation() <= ResonanceDriftNetwork::kMaxLaneDecimation);
        }
        {
            // The component's OWN default sits inside the decimated sub-range,
            // which is the whole reason FR-037 exists: a BrownianDrift's tau
            // saturates at kTauMax = 30 s, so without the decimation every rate
            // in [0.002, 0.0333] Hz - the 0.03 Hz default included - would
            // render identically while getWanderRate kept reporting the
            // requested number.
            ResonanceDriftNetwork fresh;
            fresh.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
            REQUIRE(fresh.getWanderRate()
                    == Catch::Approx(ResonanceDriftNetwork::kDefaultWanderRateHz));
            REQUIRE(fresh.getLaneDecimation() == 2u);
        }

        // (c-2) The rebase. With the FR-035 ceiling lifted out of the way the
        // realised frequency IS the lane: between advances a decimated lane
        // holds its output, so the realised frequency is bit-stable, and the
        // control steps on which it moves ARE the advance steps. That turns "no
        // extra advance, none skipped" into an exact observation of the advance
        // lattice rather than a tolerance on a jump size.
        {
            constexpr std::size_t kDecimation = 17;
            constexpr std::size_t kStepsTotal = 120;
            constexpr std::size_t kRateChangeAfter = 40;
            // 39 % 17 = 5: the change deliberately lands BETWEEN advances, the
            // only placement that can tell a rebase from a restart.
            static_assert(((kRateChangeAfter - 1) % kDecimation) != 0,
                          "the rate change must not coincide with an advance step");

            ResonanceDriftNetwork net;
            net.setSeed(0xD1CE0B08u);
            net.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
            net.setSlewCeilings(ResonanceDriftNetwork::kMaxSlewOctaves,
                                ResonanceDriftNetwork::kMaxSlewOctaves);
            for (std::size_t i = 0; i < kNumPeaks; ++i) {
                net.setFreqWander(i, 6.0f);
            }
            net.setWanderRate(0.002f);
            REQUIRE(net.getLaneDecimation() == kDecimation);

            std::array<float, kNumPeaks> previous{};
            for (std::size_t i = 0; i < kNumPeaks; ++i) {
                previous[i] = net.getPeakCurrentFrequency(i);
            }

            for (std::size_t s = 1; s <= kStepsTotal; ++s) {
                advanceControlSteps(net, 1);
                bool movedAny = false;
                for (std::size_t i = 0; i < kNumPeaks; ++i) {
                    const float now = net.getPeakCurrentFrequency(i);
                    movedAny = movedAny || (now != previous[i]);
                    previous[i] = now;
                }
                // prepare() leaves laneCounter_ at 0, so the FIRST control step
                // advances and the lattice is s == 1 (mod 17). Asked of all
                // twelve peaks together, never of one: a lane whose walk is
                // sitting against its own output clamp can hold its value across
                // an advance, and twelve independent streams cannot.
                const bool onLattice = (((s - 1) % kDecimation) == 0);
                CAPTURE(s, movedAny, onLattice);
                REQUIRE(movedAny == onLattice);

                if (s == kRateChangeAfter) {
                    // 1 / 0.00201 = 497.5 s, and ceil(497.5 / 30) is still 17 -
                    // the SAME decimation, so the lattice must simply continue.
                    // A laneCounter_ = 0 RESTART (rather than the modulo REBASE
                    // setWanderRate performs) advances on the very next step and
                    // shifts every advance after it; that is this arm's failure
                    // mode, and nothing else in the phase looks for it.
                    net.setWanderRate(0.00201f);
                    REQUIRE(net.getLaneDecimation() == kDecimation);
                }
            }
        }

        // (c-3) The same statement at the DEFAULT ceiling, as FR-035 sees it:
        // whatever a rate change does to the lane clock, the control step that
        // follows it moves no peak further than the ceiling allows.
        {
            ResonanceDriftNetwork net;
            net.setSeed(0xC0DE0B08u);
            net.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
            advanceControlSteps(net, kSettleChunks);

            std::array<float, kNumPeaks> before{};
            for (std::size_t i = 0; i < kNumPeaks; ++i) {
                before[i] = std::log2(net.getPeakCurrentFrequency(i));
            }
            net.setWanderRate(0.005f);
            REQUIRE(net.getLaneDecimation() == 7u);

            advanceControlSteps(net, 1);
            const float ceiling = net.getFreqSlewCeiling();
            for (std::size_t i = 0; i < kNumPeaks; ++i) {
                const float moved =
                    std::abs(std::log2(net.getPeakCurrentFrequency(i)) - before[i]);
                CAPTURE(i, moved, ceiling);
                REQUIRE(moved <= ceiling + 1.0e-4f);
            }
        }
    }

    SECTION("(d) setWanderEnabled(false) zeroes the depths without rewinding the lanes") {
        const auto configure = [](ResonanceDriftNetwork& net) {
            net.setSeed(0xFA5E0B08u);
            net.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
            for (std::size_t i = 0; i < kNumPeaks; ++i) {
                net.setFreqWander(i, 6.0f);
            }
            net.setWanderRate(ResonanceDriftNetwork::kMaxWanderRateHz);
        };

        // Two identically seeded instances driven with the SAME number of
        // control steps throughout, so their lanes stay in lockstep by
        // construction: `frozen` switches wander off and back on, `running`
        // never does. Every assertion below is a statement about that difference.
        ResonanceDriftNetwork frozen;
        ResonanceDriftNetwork running;
        configure(frozen);
        configure(running);

        constexpr std::size_t kLeg = 3000;  // 4 s at 48 kHz = four lane time constants
        advanceControlSteps(frozen, kLeg);
        advanceControlSteps(running, kLeg);

        frozen.setWanderEnabled(false);
        REQUIRE_FALSE(frozen.isWanderEnabled());
        advanceControlSteps(frozen, kLeg);
        advanceControlSteps(running, kLeg);

        // (d-1) With every depth scaled to zero each peak sits exactly ON its
        // anchor - the FR-035 ceiling carried it there over ~25 control steps,
        // and kLeg is two orders past that.
        for (std::size_t i = 0; i < kNumPeaks; ++i) {
            const float cents = centsBetween(frozen.getPeakCurrentFrequency(i),
                                             frozen.getPeakAnchorHz(i));
            CAPTURE(i, cents);
            REQUIRE(std::abs(cents) <= 1.0f);
        }
        // NON-VACUITY: the twin, still wandering, is NOT on its anchor, so
        // (d-1) is a statement about the switch and not about a dead lane.
        int departed = 0;
        for (std::size_t i = 0; i < kNumPeaks; ++i) {
            if (std::abs(centsBetween(running.getPeakCurrentFrequency(i),
                                      running.getPeakAnchorHz(i)))
                > 10.0f) {
                ++departed;
            }
        }
        CAPTURE(departed);
        REQUIRE(departed >= 8);

        // (d-2) Re-enabling does not SNAP. The target jumps straight back to
        // wherever the still-running lane has drifted to, and the FR-035 ceiling
        // - not a re-snap of the control state - is what carries the applied
        // value there. A build that called snapControlState() on re-enable is
        // red on exactly this assertion and on nothing else in the phase.
        std::array<float, kNumPeaks> before{};
        for (std::size_t i = 0; i < kNumPeaks; ++i) {
            before[i] = std::log2(frozen.getPeakCurrentFrequency(i));
        }
        frozen.setWanderEnabled(true);
        advanceControlSteps(frozen, 1);
        advanceControlSteps(running, 1);  // keep the two lane clocks in lockstep

        const float ceiling = frozen.getFreqSlewCeiling();
        for (std::size_t i = 0; i < kNumPeaks; ++i) {
            const float moved =
                std::abs(std::log2(frozen.getPeakCurrentFrequency(i)) - before[i]);
            CAPTURE(i, moved, ceiling);
            REQUIRE(moved <= ceiling + 1.0e-4f);
        }

        // (d-3) The lanes were never rewound. Once the ceiling has carried the
        // frozen instance back onto its (still-moving) target it agrees with the
        // twin that never switched off - which is true only if its four lanes
        // kept advancing through the whole disabled interval. A build that
        // called lane.reset() on either edge lands somewhere else entirely.
        advanceControlSteps(frozen, kLeg);
        advanceControlSteps(running, kLeg);
        int stillDeparted = 0;
        for (std::size_t i = 0; i < kNumPeaks; ++i) {
            const float cents = centsBetween(frozen.getPeakCurrentFrequency(i),
                                             running.getPeakCurrentFrequency(i));
            CAPTURE(i, cents);
            REQUIRE(std::abs(cents) <= 1.0f);
            if (std::abs(centsBetween(running.getPeakCurrentFrequency(i),
                                      running.getPeakAnchorHz(i)))
                > 10.0f) {
                ++stillDeparted;
            }
        }
        // ...and that agreement is not the trivial one of two instances both
        // sitting on their anchors.
        CAPTURE(stillDeparted);
        REQUIRE(stillDeparted >= 8);
    }
}

// =============================================================================
// T009 - the engine seam, the FR-014/FR-015 write pair and the FR-044 render
// tail. These two cases are the first in this TU that require the network to
// make sound at all: every arm above holds either at mix = 0 or on the control
// surface, and passes against a build whose renderChunk emits digital silence.
// =============================================================================

namespace {

/// FR-007's absolute grid is the whole subject of the invariance case, so the
/// partition deliberately mixes lengths BELOW one control chunk (1, 3, 63),
/// EQUAL to it (64), and above it both aligned (512, 1024) and not (65, 127,
/// 200). A block-relative chunking - HarmonicCloud's shape, which this
/// component deliberately does not copy (header, processBlock) - runs TWO
/// control steps for the 36 + 28 split an unsplit 64 runs once, and lands
/// outside kSampleTolerance within a second of render.
constexpr std::array<std::size_t, 9> kIrregularPartition{1, 63, 64, 65, 200, 512, 1024, 3, 127};

} // namespace

TEST_CASE("ResonanceDriftNetwork_BlockSizeInvariance", "[resonance_drift_network]") {
    // SC-010. Same binary, same process, two FRESH identically seeded
    // instances, NO stored golden - the comparison is between two live renders,
    // which is what keeps this criterion free of the bit-exact float golden the
    // repo forbids (tools/lint-float-bit-goldens.js).
    constexpr std::size_t kTotal = static_cast<std::size_t>(5.0 * kFs);  // 240 000
    constexpr std::uint32_t kSeed = 0xB10C5152u;

    std::vector<float> inL(kTotal);
    std::vector<float> inR(kTotal);
    fillDrive(inL, inR, 0);

    ResonanceDriftNetwork uniform;
    uniform.setSeed(kSeed);
    uniform.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
    std::vector<float> uniL(kTotal, 0.0f);
    std::vector<float> uniR(kTotal, 0.0f);
    for (std::size_t done = 0; done < kTotal;) {
        const std::size_t chunk = std::min(std::size_t{512}, kTotal - done);
        uniform.processBlock(inL.data() + done, inR.data() + done,
                             uniL.data() + done, uniR.data() + done, chunk);
        done += chunk;
    }

    ResonanceDriftNetwork ragged;
    ragged.setSeed(kSeed);
    ragged.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
    std::vector<float> ragL(kTotal, 0.0f);
    std::vector<float> ragR(kTotal, 0.0f);
    for (std::size_t done = 0, k = 0; done < kTotal; ++k) {
        const std::size_t want = kIrregularPartition[k % kIrregularPartition.size()];
        const std::size_t chunk = std::min(want, kTotal - done);
        ragged.processBlock(inL.data() + done, inR.data() + done,
                            ragL.data() + done, ragR.data() + done, chunk);
        done += chunk;
    }

    // Non-vacuity first: two silent renders agree perfectly.
    REQUIRE(peakAbs(uniL) > 0.0f);
    REQUIRE(peakAbs(uniR) > 0.0f);

    const float diffL = maxAbsDiff(uniL, ragL);
    const float diffR = maxAbsDiff(uniR, ragR);
    CAPTURE(diffL, diffR);
    REQUIRE(diffL <= Krate::DSP::TestUtils::kSampleTolerance);
    REQUIRE(diffR <= Krate::DSP::TestUtils::kSampleTolerance);
}

TEST_CASE("ResonanceDriftNetwork_RendersNonSilent", "[resonance_drift_network]") {
    // The FR-016 default patch at mix = 1, driven by white noise. This is the
    // case that fails on a renderChunk whose wet sum is still identically zero,
    // on a seam that never enables a bank slot, and on a control step that
    // never writes frequency/Q/gain into the bank at all.
    constexpr std::size_t kTotal = static_cast<std::size_t>(2.0 * kFs);  // 96 000

    std::vector<float> inL(kTotal);
    std::vector<float> inR(kTotal);
    fillNoiseAtDbfs(inL, 0x2468ACE0u, -12.0f);
    fillNoiseAtDbfs(inR, 0x13579BDFu, -12.0f);

    // The drive is what the criterion says it is, not approximately so.
    const float driveDbL = Krate::DSP::gainToDb(rmsOf(inL));
    const float driveDbR = Krate::DSP::gainToDb(rmsOf(inR));
    CAPTURE(driveDbL, driveDbR);
    REQUIRE(std::abs(driveDbL + 12.0f) <= 0.01f);
    REQUIRE(std::abs(driveDbR + 12.0f) <= 0.01f);

    ResonanceDriftNetwork net;
    net.setSeed(0x9E5511E5u);
    net.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
    net.setMix(1.0f);

    std::vector<float> outL(kTotal, 0.0f);
    std::vector<float> outR(kTotal, 0.0f);
    for (std::size_t done = 0; done < kTotal;) {
        const std::size_t chunk = std::min(std::size_t{512}, kTotal - done);
        net.processBlock(inL.data() + done, inR.data() + done,
                         outL.data() + done, outR.data() + done, chunk);
        done += chunk;
    }

    REQUIRE(allFinite(outL));
    REQUIRE(allFinite(outR));

    const float dbL = Krate::DSP::gainToDb(rmsOf(outL));
    const float dbR = Krate::DSP::gainToDb(rmsOf(outR));
    CAPTURE(dbL, dbR);
    REQUIRE(dbL > -60.0f);
    REQUIRE(dbR > -60.0f);
}


// =============================================================================
// T010 arm set - FR-041's gate ramp, the C-6 base-level ramp and FR-043's mix
// smoother (SC-002 (d), (d-ii), (d-iii), (d-iv))
// =============================================================================

namespace {

/// SC-002 (d)'s per-sample gate-step bound: `1.05 / (kGainRampMs * 0.001 * fs)`.
/// The 5 % margin sits above the correct worst case of `1 / 2400 = 4.1667e-4`,
/// which is the increment `LinearRamp` derives for a full 0 -> 1 move
/// (smoother.h:100-108).
constexpr float kGateStepBound =
    1.05f / (ResonanceDriftNetwork::kGainRampMs * 0.001f * static_cast<float>(kFs));
static_assert(kGateStepBound > 4.3e-4f && kGateStepBound < 4.5e-4f,
              "the SC-002 (d) bound is 4.375e-4 at 48 kHz");

/// The exact length of a full FR-041 ramp in samples.
constexpr std::size_t kGateRampSamples = static_cast<std::size_t>(
    ResonanceDriftNetwork::kGainRampMs * 0.001f * static_cast<float>(kFs));
static_assert(kGateRampSamples == 2400u, "every millisecond figure below assumes 48 kHz");

/// The +/- 5 ms FR-041 admits, as the Catch2 margin these arms use.
constexpr float kRampToleranceMs = 5.0f;

/// Render `ms` milliseconds of silence in whole control chunks.
void renderSilence(ResonanceDriftNetwork& net, float ms) {
    const auto total = static_cast<std::size_t>(ms * 0.001f * static_cast<float>(kFs));
    std::array<float, kControlChunk> zeros{};
    std::array<float, kControlChunk> oL{};
    std::array<float, kControlChunk> oR{};
    for (std::size_t done = 0; done < total;) {
        const std::size_t chunk = std::min(kControlChunk, total - done);
        net.processBlock(zeros.data(), zeros.data(), oL.data(), oR.data(), chunk);
        done += chunk;
    }
}

/// Render `numSamples` samples ONE AT A TIME - the `numSamples == 1` granularity
/// SC-002 (d) names, and which FR-003 admits - recording `getPeakGate(peak)`
/// after each. `schedule` runs at sample 0 and then every `blockSamples`
/// samples (never, when `blockSamples == 0`), which is how the (d-ii) arm
/// reproduces FR-040's once-per-block Phase-10 scheduler WITHOUT coarsening the
/// measurement: the gate is still read every single sample.
template <typename Schedule>
[[nodiscard]] std::vector<float> traceGate(ResonanceDriftNetwork& net, std::size_t peak,
                                           std::size_t numSamples, std::size_t blockSamples,
                                           const Schedule& schedule) {
    std::vector<float> trace;
    trace.reserve(numSamples);
    float in = 0.0f;
    float oL = 0.0f;
    float oR = 0.0f;
    for (std::size_t n = 0; n < numSamples; ++n) {
        if (blockSamples != 0 && (n % blockSamples) == 0) {
            schedule(n);
        }
        net.processBlock(&in, &in, &oL, &oR, 1);
        trace.push_back(net.getPeakGate(peak));
    }
    return trace;
}

/// Time, in ms, at which `trace` first reaches `threshold`; negative when it
/// never does inside the traced window. Entry `i` is the gate AFTER sample
/// `i + 1` has been rendered, so the count is `i + 1` and not `i`.
[[nodiscard]] float msToReach(const std::vector<float>& trace, float threshold) {
    for (std::size_t i = 0; i < trace.size(); ++i) {
        if (trace[i] >= threshold) {
            return 1000.0f * static_cast<float>(i + 1) / static_cast<float>(kFs);
        }
    }
    return -1.0f;
}

/// The largest single-sample RISE in `trace`, counting the step out of
/// `startValue` into `trace[0]` - without that first term a build that jumped
/// the whole way before the first traced sample would read a step of zero.
[[nodiscard]] float maxRise(const std::vector<float>& trace, float startValue) {
    float worst = 0.0f;
    float previous = startValue;
    for (const float v : trace) {
        worst = std::max(worst, v - previous);
        previous = v;
    }
    return worst;
}

[[nodiscard]] bool isMonotoneNonDecreasing(const std::vector<float>& trace, float startValue) {
    float previous = startValue;
    for (const float v : trace) {
        if (v < previous) return false;
        previous = v;
    }
    return true;
}

/// Render `count` samples of a sine at `hz`, continuing the phase from absolute
/// sample `startIndex`, in whole control chunks. The tone is identical on both
/// input channels (the Success Criteria section's stereo drive convention); the
/// LEFT output is appended to `outLeft` when that is non-null.
void renderTone(ResonanceDriftNetwork& net, float hz, float amplitude,
                std::size_t startIndex, std::size_t count, std::vector<float>* outLeft) {
    std::array<float, kControlChunk> inBuf{};
    std::array<float, kControlChunk> oL{};
    std::array<float, kControlChunk> oR{};
    constexpr double kTwoPiD = 6.28318530717958647692;
    const double omega = kTwoPiD * static_cast<double>(hz) / kFs;
    for (std::size_t done = 0; done < count;) {
        const std::size_t chunk = std::min(kControlChunk, count - done);
        for (std::size_t k = 0; k < chunk; ++k) {
            const auto n = static_cast<double>(startIndex + done + k);
            inBuf[k] = amplitude * static_cast<float>(std::sin(omega * n));
        }
        net.processBlock(inBuf.data(), inBuf.data(), oL.data(), oR.data(), chunk);
        if (outLeft != nullptr) {
            outLeft->insert(outLeft->end(), oL.begin(),
                            oL.begin() + static_cast<std::ptrdiff_t>(chunk));
        }
        done += chunk;
    }
}

/// SC-002 (d)'s NAMED audio-domain estimator: the per-cycle peak magnitude over
/// consecutive `round(fs / f)`-sample windows. Every tone frequency used below
/// divides 48 000 exactly (48000/1200 = 40, 48000/2000 = 24), so the window is
/// a whole number of periods and the estimator contributes NO ripple of its own
/// to confuse with the envelope it is measuring.
[[nodiscard]] std::vector<float> perCycleEnvelope(const std::vector<float>& x,
                                                  std::size_t window) {
    std::vector<float> env;
    if (window == 0) return env;
    env.reserve(x.size() / window);
    for (std::size_t start = 0; start + window <= x.size(); start += window) {
        float worst = 0.0f;
        for (std::size_t k = 0; k < window; ++k) {
            worst = std::max(worst, std::abs(x[start + k]));
        }
        env.push_back(worst);
    }
    return env;
}

/// END time, in ms, of the first window whose envelope crosses `threshold`;
/// negative when none does. The window's END rather than its start is
/// deliberate and conservative: a per-cycle estimator resolves the crossing only
/// to within one window, and the end is the first instant at which the crossing
/// is a fact rather than a possibility.
[[nodiscard]] float envelopeCrossMs(const std::vector<float>& env, std::size_t window,
                                    float threshold, bool rising) {
    for (std::size_t k = 0; k < env.size(); ++k) {
        const bool crossed = rising ? (env[k] >= threshold) : (env[k] <= threshold);
        if (crossed) {
            return 1000.0f * static_cast<float>((k + 1) * window) / static_cast<float>(kFs);
        }
    }
    return -1.0f;
}

} // namespace

TEST_CASE("ResonanceDriftNetwork_GateRamp", "[resonance_drift_network]") {
    constexpr std::size_t kMaxPeaks = ResonanceDriftNetwork::kMaxPeaks;

    SECTION("(d) a 0 -> 1 wake ramps the gate over 50 ms, monotonically, in bounded steps") {
        ResonanceDriftNetwork net;
        net.setSeed(0x6A7E0001u);
        net.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
        net.setWanderEnabled(false);

        constexpr std::size_t kPeak = 5;

        // The precondition FR-042 states: >= 50 ms at wake == 0, so the ramp has
        // landed on EXACTLY 0.0f and the sleep edge has fired.
        net.setPeakWake(kPeak, 0.0f);
        renderSilence(net, 100.0f);
        REQUIRE(net.getPeakGate(kPeak) == 0.0f);
        REQUIRE(net.isPeakEngineActive(kPeak) == false);

        net.setPeakWake(kPeak, 1.0f);
        // The wake edge lives in the SETTER, so the engine is live BEFORE the
        // first sample of the ramp - FR-042's "the first audible sample is
        // already filtered". A build that deferred it to the next control step
        // would read false here.
        REQUIRE(net.isPeakEngineActive(kPeak) == true);

        const std::vector<float> trace =
            traceGate(net, kPeak, 4 * kGateRampSamples, 0, [](std::size_t) {});

        const float reachedMs = msToReach(trace, 1.0f);
        const float rise = maxRise(trace, 0.0f);
        CAPTURE(reachedMs, rise, kGateStepBound);
        REQUIRE(reachedMs > 0.0f);
        REQUIRE(reachedMs == Catch::Approx(50.0f).margin(kRampToleranceMs));
        REQUIRE(isMonotoneNonDecreasing(trace, 0.0f));
        REQUIRE(rise <= kGateStepBound);
    }

    SECTION("(d) secondary - the per-cycle audio envelope reaches 90 % over the same 50 ms") {
        constexpr std::size_t kPeak = 11;
        constexpr float kToneHz = 1200.0f;
        constexpr std::size_t kCycle = 40;  // round(48000 / 1200), exact
        static_assert(static_cast<std::size_t>(kFs) == kCycle * 1200u,
                      "the per-cycle window must be a whole number of periods");

        ResonanceDriftNetwork net;
        net.setSeed(0x6A7E0002u);
        net.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
        net.setWanderEnabled(false);
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            if (i != kPeak) {
                net.setPeakDormant(i, true);
            }
        }
        // Retuned onto the drive so the peak sits at its own centre frequency,
        // which is what the criterion asks for, AND so the estimator window is
        // an exact period count.
        net.setPeakAnchorHz(kPeak, kToneHz);
        // Q = 60, not the FR-016 default of 12, and the choice is measured
        // rather than arbitrary. The audio envelope on a wake is
        // gate(t) * (1 - exp(-t / tau)) with tau = Q / (pi * f0): at Q = 12 that
        // is tau = 3.2 ms, the filter term is 1 within a few ms, and the 90 %
        // crossing lands on 45.0 ms - EXACTLY on the lower edge of 50 ms +/- 5 ms,
        // where the arm would be decided by the last bit of a float. At Q = 60,
        // tau = 15.9 ms puts the crossing at ~47.5 ms, inside the band with
        // margin at both ends. It is also the harder configuration for FR-042's
        // state clear: 0.11 s of stored ring rather than 0.02 s.
        net.setPeakQ(kPeak, 60.0f);
        // FR-045's default trim is +30 dB (PROVISIONAL until tasks.md T019).
        // Driven at resonance by a 0.25 tone that lands 9.1x full scale, which
        // FR-018's +/- 4.0 clamp would flatten - and a clamped envelope is not
        // the ramp this arm is measuring. 0 dB is an in-spec public setting.
        net.setWetGain(0.0f);

        std::size_t clock = 0;
        constexpr auto kSecond = static_cast<std::size_t>(kFs);
        renderTone(net, kToneHz, 0.25f, clock, kSecond, nullptr);  // settle everything
        clock += kSecond;

        std::vector<float> steady;
        renderTone(net, kToneHz, 0.25f, clock, 10 * kCycle, &steady);
        clock += 10 * kCycle;
        const std::vector<float> steadyEnv = perCycleEnvelope(steady, kCycle);
        REQUIRE(!steadyEnv.empty());
        const float reference = steadyEnv.back();
        REQUIRE(reference > 0.0f);  // non-vacuity: there is an envelope to measure

        net.setPeakWake(kPeak, 0.0f);
        renderTone(net, kToneHz, 0.25f, clock, kSecond / 5, nullptr);  // 200 ms
        clock += kSecond / 5;
        REQUIRE(net.isPeakEngineActive(kPeak) == false);

        net.setPeakWake(kPeak, 1.0f);
        std::vector<float> woken;
        renderTone(net, kToneHz, 0.25f, clock, kSecond / 10, &woken);  // 100 ms

        const std::vector<float> env = perCycleEnvelope(woken, kCycle);
        const float ninetyMs = envelopeCrossMs(env, kCycle, 0.9f * reference, true);
        CAPTURE(reference, ninetyMs);
        REQUIRE(ninetyMs > 0.0f);
        // No per-sample bound is asserted on audio (SC-002 (d)): the observable
        // is an oscillating waveform, and any follower used to extract an
        // envelope would impose its own smoothing and dominate the statistic.
        REQUIRE(ninetyMs == Catch::Approx(50.0f).margin(kRampToleranceMs));
    }

    SECTION("(d-ii) refreshGates re-target immunity - a scheduler running on ANOTHER peak") {
        ResonanceDriftNetwork net;
        net.setSeed(0x6A7E0003u);
        net.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
        net.setWanderEnabled(false);

        constexpr std::size_t kPeak = 5;
        constexpr std::size_t kOther = 2;
        constexpr std::size_t kSchedulerBlock = 512;

        net.setPeakWake(kPeak, 0.0f);
        renderSilence(net, 100.0f);
        REQUIRE(net.getPeakGate(kPeak) == 0.0f);

        net.setPeakWake(kPeak, 1.0f);
        REQUIRE(net.isPeakEngineActive(kPeak) == true);

        // FR-040's Phase-10 scheduler shape: ONE setPeakWake per block, on a
        // peak that is NOT the one being measured, with a value that really
        // moves every time. Both values stay far above kWakeSilenceEpsilon, so
        // kOther never crosses a sleep or a wake edge and cannot perturb the
        // measurement by any route other than refreshGates() itself.
        std::size_t calls = 0;
        const auto schedule = [&net, &calls](std::size_t) {
            net.setPeakWake(kOther, ((calls++ % 2u) == 0u) ? 0.55f : 0.85f);
        };

        const std::vector<float> trace =
            traceGate(net, kPeak, 12 * kGateRampSamples, kSchedulerBlock, schedule);

        // RED-FIRST READINGS, recorded for tasks.md T024. Against a
        // refreshGates() that re-targets every peak unconditionally, each call
        // restarts LinearRamp's 50 ms clock (smoother.h:342-354) and the
        // remaining travel shrinks by only 1 - 512/2400 = 0.787 per block:
        // ~107 ms to 90 %, ~213 ms to 99 %, and 1.0f is never reached at all
        // inside this window. With the lastGateTarget guard they are ~45 ms,
        // ~49.5 ms and 50 ms.
        const float ninetyMs = msToReach(trace, 0.9f);
        const float ninetyNineMs = msToReach(trace, 0.99f);
        const float reachedMs = msToReach(trace, 1.0f);
        const float rise = maxRise(trace, 0.0f);
        CAPTURE(calls, ninetyMs, ninetyNineMs, reachedMs, rise);

        REQUIRE(calls > 0u);
        REQUIRE(reachedMs > 0.0f);
        REQUIRE(reachedMs == Catch::Approx(50.0f).margin(kRampToleranceMs));
        REQUIRE(isMonotoneNonDecreasing(trace, 0.0f));
        // This one passes on the broken build too - re-targeting only ever
        // LOWERS the per-sample step. It is here for completeness, not as the
        // discriminating assertion.
        REQUIRE(rise <= kGateStepBound);
    }

    SECTION("(d-iii) the C-6 base-level ramp - setPeakLevel does not step 72 dB in one sample") {
        constexpr std::size_t kPeak = 11;
        constexpr float kToneHz = 1200.0f;
        constexpr std::size_t kCycle = 40;

        // 1.05 * dbToGain(+12) / (kGainRampMs * 0.001 * fs) = 1.7417e-3 at 48 kHz.
        const float levelStepBound =
            1.05f * Krate::DSP::dbToGain(12.0f)
            / (ResonanceDriftNetwork::kGainRampMs * 0.001f * static_cast<float>(kFs));

        ResonanceDriftNetwork net;
        net.setSeed(0x6A7E0004u);
        net.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
        net.setWanderEnabled(false);
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            if (i != kPeak) {
                net.setPeakDormant(i, true);
            }
        }
        net.setPeakAnchorHz(kPeak, kToneHz);
        net.setPeakLevel(kPeak, -60.0f);
        // Headroom for the +12 dB end of the move: at FR-045's provisional
        // +30 dB default this render would sit 9x past FR-018's +/- 4.0 clamp,
        // and a clamped envelope is not the ramp this arm is measuring.
        net.setWetGain(0.0f);

        std::size_t clock = 0;
        constexpr auto kSecond = static_cast<std::size_t>(kFs);
        renderTone(net, kToneHz, 0.25f, clock, kSecond, nullptr);
        clock += kSecond;

        // With wander off the joint FR-017 clamp is inactive, so appliedGainDb
        // tracks levelDb exactly and appliedBankGainDb stays at 0 dB: NOTHING
        // about this move reaches the bank, and the rendered envelope is a pure
        // image of Peak::levelRamp.
        std::vector<float> before;
        renderTone(net, kToneHz, 0.25f, clock, 10 * kCycle, &before);
        clock += 10 * kCycle;
        const std::vector<float> beforeEnv = perCycleEnvelope(before, kCycle);
        REQUIRE(!beforeEnv.empty());
        const float initial = beforeEnv.back();

        net.setPeakLevel(kPeak, 12.0f);
        std::vector<float> rendered;
        renderTone(net, kToneHz, 0.25f, clock, kSecond / 5, &rendered);  // 200 ms

        const std::vector<float> env = perCycleEnvelope(rendered, kCycle);
        REQUIRE(env.size() > 100u);
        const float settled = env.back();
        REQUIRE(settled > 0.0f);
        REQUIRE(settled > 100.0f * initial);  // non-vacuity: the 72 dB move happened

        // Express the envelope in the LEVEL RAMP's own units, so the bound -
        // which is written about a linear gain - is comparable: at settle the
        // ramp sits at dbToGain(+12), so scaling by dbToGain(12) / settled maps
        // the envelope onto the gain trajectory. The window before the setter
        // call is the trajectory's starting point; WITHOUT it a build that
        // jumped instantaneously would show only flat windows and read a step
        // of zero.
        const float toGain = Krate::DSP::dbToGain(12.0f) / settled;
        float worstPerSample = 0.0f;
        float previous = initial;
        for (const float e : env) {
            const float delta = (e - previous) * toGain;
            // The estimator resolves one window; the bound is per sample, so the
            // window's rise is spread across its kCycle samples.
            worstPerSample = std::max(worstPerSample, delta / static_cast<float>(kCycle));
            previous = e;
        }

        const float riseMs = envelopeCrossMs(env, kCycle, 0.99f * settled, true);
        CAPTURE(initial, settled, riseMs, worstPerSample, levelStepBound);
        REQUIRE(riseMs > 0.0f);
        REQUIRE(riseMs == Catch::Approx(50.0f).margin(kRampToleranceMs));
        REQUIRE(worstPerSample <= levelStepBound);
    }

    SECTION("(d-iv) FR-043's mix smoother - setMix is a 20 ms crossfade, not a step") {
        constexpr float kToneHz = 2000.0f;
        constexpr std::size_t kCycle = 24;  // round(48000 / 2000), exact
        static_assert(static_cast<std::size_t>(kFs) == kCycle * 2000u,
                      "the per-cycle window must be a whole number of periods");

        ResonanceDriftNetwork net;
        net.setSeed(0x6A7E0005u);
        net.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
        net.setWanderEnabled(false);
        // The precondition below is about the RESIDUAL wet path, and FR-045's
        // trim is what sets its level: kDefaultWetGainDb is +34.5 dB (MEASURED
        // at tasks.md T019), which lifts even a far-off-passband residual to
        // within a few dB of the dry signal. Pinning the trim to its
        // documented minimum is an in-spec public setting, and the precondition
        // is still REQUIREd below on a MEASUREMENT rather than assumed.
        net.setWetGain(ResonanceDriftNetwork::kMinWetGainDb);

        std::size_t clock = 0;
        constexpr auto kSecond = static_cast<std::size_t>(kFs);

        // mix = 1 is the FR-016 default, so this render IS the wet path.
        renderTone(net, kToneHz, 0.25f, clock, kSecond / 5, nullptr);
        clock += kSecond / 5;
        std::vector<float> wet;
        renderTone(net, kToneHz, 0.25f, clock, kSecond / 10, &wet);
        clock += kSecond / 10;

        net.setMix(0.0f);
        renderTone(net, kToneHz, 0.25f, clock, kSecond / 5, nullptr);
        clock += kSecond / 5;
        std::vector<float> dry;
        renderTone(net, kToneHz, 0.25f, clock, kSecond / 10, &dry);
        clock += kSecond / 10;

        const float wetDb = Krate::DSP::gainToDb(rmsOf(wet));
        const float dryDb = Krate::DSP::gainToDb(rmsOf(dry));
        CAPTURE(wetDb, dryDb);
        // PRECONDITION: a 2 kHz sine sits outside every default peak's passband,
        // so the envelope measured through the transition tracks (1 - m) and
        // nothing else.
        REQUIRE(wetDb <= dryDb - 40.0f);

        const std::vector<float> dryEnv = perCycleEnvelope(dry, kCycle);
        REQUIRE(!dryEnv.empty());
        const float initial = dryEnv.back();
        REQUIRE(initial > 0.0f);

        net.setMix(1.0f);
        std::vector<float> fade;
        renderTone(net, kToneHz, 0.25f, clock, (3u * kSecond) / 50u, &fade);  // 60 ms

        const std::vector<float> env = perCycleEnvelope(fade, kCycle);
        // OnePoleSmoother::configure's documented contract is "time to reach 99 %
        // of target" (smoother.h:158-159), so (1 - m) reaches 1 % of its initial
        // value in kMixSmoothMs = 20 ms. A build applying setMix instantaneously
        // reads 0 ms here.
        const float fallMs = envelopeCrossMs(env, kCycle, 0.01f * initial, false);
        CAPTURE(initial, fallMs);
        REQUIRE(fallMs > 0.0f);
        REQUIRE(fallMs == Catch::Approx(20.0f).margin(kRampToleranceMs));

        // Monotone down to the crossing, and no further: past it the rising
        // residual wet term is the larger of the two and the envelope
        // legitimately flattens out, so the assertion stops where the criterion
        // does.
        const auto crossWindows =
            static_cast<std::size_t>(fallMs * 0.001f * static_cast<float>(kFs)) / kCycle;
        bool monotone = true;
        float previous = initial;
        for (std::size_t k = 0; k < std::min(crossWindows, env.size()); ++k) {
            if (env[k] > previous + 1.0e-6f) {
                monotone = false;
            }
            previous = env[k];
        }
        REQUIRE(monotone);
    }
}

// =============================================================================
// T011 arm set - per-peak pan (FR-038, FR-039): the seeded one-shot default
// positions, their determinism under seed, and the equal-power cos/sin split
// (SC-021 (a), (b), (c)).
// =============================================================================
//
// What each arm is FOR:
//   (a) the sin/cos law's DEFINING property - constant total power across the
//       whole sweep - asserted directly rather than assumed from the formula.
//       A linear-pan build (gainL = (1 - p) / 2, gainR = (1 + p) / 2) loses
//       3 dB at centre and is red here by 3 dB against a 0.5 dB window;
//   (b) the seeded one-shot draw is a FUNCTION OF THE SEED, identical across
//       instances and stable through a render, with the alternating-side
//       spread FR-038 documents;
//   (c) the arm without which (a) and (b) both pass VACUOUSLY on a build whose
//       pan lane is dead: a static split satisfies constant power and is
//       trivially deterministic. Only a measured RANGE of inter-channel level
//       difference proves the lane is live.
// =============================================================================

namespace {

/// SC-021 (c)'s non-vacuity bound: the minimum dB RANGE of the inter-channel
/// level difference across a 60 s render at the FR-016 default pan wander.
///
/// **MEASURED, 2026-09-11**, across the eight calibration seeds: the realised
/// range read mean 3.077, sd 0.565, min 2.464, max 3.783 dB. 1 dB STANDS - it
/// is a LOWER bound and sits 1.46 dB (2.6 sd) below the observed minimum, so a
/// correct build clears it on every seed measured, while a build whose pan lane
/// is dead reads a range of exactly 0 and fails it. The calibration's
/// mechanical "observed min - 3 sd" suggestion of 0.769 would be WEAKER: for a
/// lower bound, lower is looser, and there is no reason to give away 0.23 dB of
/// discrimination when the measured margin is already 2.6 sd.
///
/// MEASURED BY: ResonanceDriftNetwork_MeasureThresholds, section
/// "SC-021 (c) kPanNonVacuityDb" (resonance_drift_network_perf_test.cpp,
/// [.calibration]). It is a LOWER bound: the suggestion sits BELOW the observed
/// minimum, because the arm exists to prove the lane is LIVE.
constexpr float kPanNonVacuityDb = 1.0f;

/// Stereo, sample-rate-parameterised twin of renderTone (above).
///
/// Both reasons are load-bearing. Stereo: SC-021 is a statement about the two
/// channels jointly (their sum of squares in (a), their ratio in (c)), and
/// renderTone captures only the left. Parameterised rate: arm (c) is a 60 s
/// render, and - exactly as the T008 lane arms argued - the lane properties it
/// measures are defined in SECONDS while FR-037's decimation is sample-rate
/// independent, so the honest rate for it is the lowest prepare() accepts.
void renderToneStereoAt(ResonanceDriftNetwork& net, double fs, float hz, float amplitude,
                        std::size_t startIndex, std::size_t count,
                        std::vector<float>* outL, std::vector<float>* outR) {
    std::array<float, kControlChunk> inBuf{};
    std::array<float, kControlChunk> oL{};
    std::array<float, kControlChunk> oR{};
    constexpr double kTwoPiD = 6.28318530717958647692;
    const double omega = kTwoPiD * static_cast<double>(hz) / fs;
    for (std::size_t done = 0; done < count;) {
        const std::size_t chunk = std::min(kControlChunk, count - done);
        for (std::size_t k = 0; k < chunk; ++k) {
            const auto n = static_cast<double>(startIndex + done + k);
            inBuf[k] = amplitude * static_cast<float>(std::sin(omega * n));
        }
        // The same tone on both channels: renderChunk folds the pair to
        // x = 0.5 * (dryL + dryR) before the engine (FR-044), so an identical
        // drive is what makes the OUTPUT stereo image purely the pan law's
        // doing rather than an image of the input's.
        net.processBlock(inBuf.data(), inBuf.data(), oL.data(), oR.data(), chunk);
        if (outL != nullptr) {
            outL->insert(outL->end(), oL.begin(),
                         oL.begin() + static_cast<std::ptrdiff_t>(chunk));
        }
        if (outR != nullptr) {
            outR->insert(outR->end(), oR.begin(),
                         oR.begin() + static_cast<std::ptrdiff_t>(chunk));
        }
        done += chunk;
    }
}

/// Isolate one peak: every OTHER peak dormant. SC-021's configuration base.
void isolatePeak(ResonanceDriftNetwork& net, std::size_t peak) {
    for (std::size_t i = 0; i < ResonanceDriftNetwork::kMaxPeaks; ++i) {
        if (i != peak) {
            net.setPeakDormant(i, true);
        }
    }
}

} // namespace

TEST_CASE("ResonanceDriftNetwork_PeakPan", "[resonance_drift_network]") {
    constexpr std::size_t kMaxPeaks = ResonanceDriftNetwork::kMaxPeaks;

    SECTION("(a) equal-power constancy across the whole [-1, +1] sweep") {
        constexpr std::size_t kPeak = 11;
        constexpr float kToneHz = 1200.0f;
        constexpr std::size_t kCycle = 40;  // round(48000 / 1200), exact
        static_assert(static_cast<std::size_t>(kFs) == kCycle * 1200u,
                      "the measurement window must be a whole number of periods");

        // 100 ms of settling per step - twice the >= 50 ms the criterion asks
        // for - then 300 WHOLE cycles (250 ms) of measurement, so the RMS
        // carries no partial-period ripple of its own.
        constexpr std::size_t kSettle = static_cast<std::size_t>(kFs) / 10;
        constexpr std::size_t kMeasure = 300u * kCycle;
        constexpr std::size_t kSteps = 21;  // -1.0 to +1.0 in 0.1 steps

        ResonanceDriftNetwork net;
        net.setSeed(0x9A110001u);
        net.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
        // Wander off, so the realised pan IS the swept value and nothing else
        // moves between steps (SC-021's configuration base).
        net.setWanderEnabled(false);
        isolatePeak(net, kPeak);
        net.setPeakAnchorHz(kPeak, kToneHz);
        // FR-045's default trim is +30 dB (PROVISIONAL until tasks.md T019).
        // Driven at resonance that lands near FR-018's +/- 4.0 clamp, and a
        // clamped channel is no longer an image of the pan law. 0 dB is an
        // in-spec public setting (kMinWetGainDb is -24).
        net.setWetGain(0.0f);

        // (k - 10) / 10 rather than -1 + 0.1 * k, deliberately: the latter is
        // FMA-contractible under /fp:fast and -ffast-math, and a single-rounded
        // fma(0.1f, 10.0f, -1.0f) is 1.49e-8, not zero - which would put the
        // sweep's centre step OFF centre and break the exact -1 / 0 / +1
        // endpoints this arm reasons about. This form has no contraction to
        // make and lands on all three exactly, on every toolchain.
        const auto panAtStep = [](std::size_t k) {
            const float raw = (static_cast<float>(k) - 10.0f) / 10.0f;
            return std::clamp(raw, -1.0f, 1.0f);
        };
        constexpr std::size_t kCentreStep = 10;
        REQUIRE(panAtStep(kCentreStep) == 0.0f);
        REQUIRE(panAtStep(0) == -1.0f);
        REQUIRE(panAtStep(kSteps - 1) == 1.0f);

        std::size_t clock = 0;
        // Settle the dormancy gate ramps, the anchor slew and the filter alike.
        constexpr auto kHalfSecond = static_cast<std::size_t>(kFs) / 2;
        renderToneStereoAt(net, kFs, kToneHz, 0.25f, clock, kHalfSecond, nullptr, nullptr);
        clock += kHalfSecond;

        std::array<float, kSteps> powerDb{};
        std::array<float, kSteps> rmsL{};
        std::array<float, kSteps> rmsR{};

        for (std::size_t k = 0; k < kSteps; ++k) {
            const float pan = panAtStep(k);
            net.setPeakPan(kPeak, pan);
            REQUIRE(net.getPeakPan(kPeak) == Catch::Approx(pan));

            renderToneStereoAt(net, kFs, kToneHz, 0.25f, clock, kSettle, nullptr, nullptr);
            clock += kSettle;

            std::vector<float> left;
            std::vector<float> right;
            renderToneStereoAt(net, kFs, kToneHz, 0.25f, clock, kMeasure, &left, &right);
            clock += kMeasure;

            rmsL[k] = rmsOf(left);
            rmsR[k] = rmsOf(right);
            const float power = (rmsL[k] * rmsL[k]) + (rmsR[k] * rmsR[k]);
            CAPTURE(k, pan, rmsL[k], rmsR[k], power);
            REQUIRE(power > 0.0f);  // non-vacuity: there is a signal to split
            powerDb[k] = 10.0f * std::log10(power);
        }

        const float referenceDb = powerDb[kCentreStep];
        float worstDeviationDb = 0.0f;
        std::size_t worstStep = 0;
        for (std::size_t k = 0; k < kSteps; ++k) {
            const float deviation = std::abs(powerDb[k] - referenceDb);
            if (deviation > worstDeviationDb) {
                worstDeviationDb = deviation;
                worstStep = k;
            }
        }
        CAPTURE(referenceDb, worstDeviationDb, worstStep);
        REQUIRE(worstDeviationDb <= 0.5f);

        // NON-VACUITY of the sweep itself. Constant power is also what a build
        // that ignored setPeakPan entirely would report, so the arm asserts that
        // the field really moved: pan = -1 is hard left (theta = 0, so
        // sin(theta) = 0 and the wet right channel is EXACTLY zero at mix = 1),
        // pan = +1 hard right, pan = 0 balanced.
        CAPTURE(rmsL[0], rmsR[0], rmsL[kSteps - 1], rmsR[kSteps - 1]);
        REQUIRE(rmsL[0] > 0.0f);
        REQUIRE(rmsR[0] <= 0.01f * rmsL[0]);
        REQUIRE(rmsR[kSteps - 1] > 0.0f);
        REQUIRE(rmsL[kSteps - 1] <= 0.01f * rmsR[kSteps - 1]);
        REQUIRE(rmsL[kCentreStep] == Catch::Approx(rmsR[kCentreStep]).epsilon(0.01));
    }

    SECTION("(b) determinism under seed - default positions and live trajectories") {
        constexpr std::uint32_t kSeed = 0x9A110002u;

        ResonanceDriftNetwork netA;
        ResonanceDriftNetwork netB;
        netA.setSeed(kSeed);
        netB.setSeed(kSeed);
        netA.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
        netB.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});

        // Preconditions, MEASURED rather than assumed: the trajectory half of
        // this arm is only meaningful while both the pan depth and the wander
        // rate are non-zero, and both are FR-016 defaults this arm never sets.
        REQUIRE(netA.isWanderEnabled());
        REQUIRE(netA.getWanderRate() == Catch::Approx(0.03f));

        bool defaultsIdentical = true;
        bool depthsAtDefault = true;
        bool sidesAlternate = true;
        bool positionsInRange = true;
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            const float panA = netA.getPeakPan(i);
            if (panA != netB.getPeakPan(i)) {  // EXACT: same seed, same draw
                defaultsIdentical = false;
            }
            if (netA.getPeakPanWander(i) != Catch::Approx(0.2f)) {
                depthsAtDefault = false;
            }
            if (!(std::abs(panA) <= 1.0f)) {
                positionsInRange = false;
            }
            // FR-038's documented spread: |base| >= 1/6 while |jitter| <= 1/12,
            // so the side can never flip - even peaks right, odd peaks left, and
            // adjacent peaks therefore never share a side.
            const bool expectedRight = ((i % 2u) == 0u);
            if (expectedRight ? !(panA > 0.0f) : !(panA < 0.0f)) {
                sidesAlternate = false;
            }
        }
        REQUIRE(defaultsIdentical);
        REQUIRE(depthsAtDefault);
        REQUIRE(positionsInRange);
        REQUIRE(sidesAlternate);

        // NON-VACUITY of the draw: it is a function of the SEED, not a constant
        // table that would satisfy "identical across instances" trivially.
        ResonanceDriftNetwork other;
        other.setSeed(kSeed ^ 0x5BD1E995u);
        other.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
        bool anySeedDifference = false;
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            if (other.getPeakPan(i) != netA.getPeakPan(i)) {
                anySeedDifference = true;
            }
        }
        REQUIRE(anySeedDifference);

        // Live trajectories, sampled once per control step over 10 s. Scanned
        // into ONE bool rather than 90 000 REQUIREs (this file's convention).
        constexpr std::size_t kTrajectorySteps =
            (10u * static_cast<std::size_t>(kFs)) / kControlChunk;  // 7500
        bool trajectoriesIdentical = true;
        for (std::size_t s = 0; s < kTrajectorySteps; ++s) {
            advanceControlSteps(netA, 1);
            advanceControlSteps(netB, 1);
            for (std::size_t i = 0; i < kMaxPeaks; ++i) {
                if (netA.getPeakCurrentPan(i) != netB.getPeakCurrentPan(i)) {
                    trajectoriesIdentical = false;
                }
            }
        }
        REQUIRE(trajectoriesIdentical);

        // NON-VACUITY of the trajectory: two FROZEN fields are also identical.
        float worstExcursion = 0.0f;
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            worstExcursion =
                std::max(worstExcursion, std::abs(netA.getPeakCurrentPan(i) - netA.getPeakPan(i)));
        }
        CAPTURE(worstExcursion);
        REQUIRE(worstExcursion > 1.0e-4f);
    }

    SECTION("(c) non-vacuity - the pan lane really moves the stereo field") {
        constexpr std::size_t kPeak = 0;
        // Peak 0's FR-016 anchor. It is also the peak whose seeded position sits
        // nearest the centre (|base| = 1/6), which keeps BOTH channels well away
        // from zero across the whole +/- 0.2 wander excursion - the ratio below
        // would diverge at a hard-panned position.
        constexpr float kToneHz = 40.0f;
        constexpr double kFsC = kSlowFs;                             // 8000 Hz
        constexpr auto kWindow = static_cast<std::size_t>(kSlowFs);  // exactly 1 s
        constexpr std::size_t kWindows = 60;                         // >= 60 s
        static_assert((kWindow % kControlChunk) == 0u,
                      "each 1 s window must be a whole number of control chunks");

        ResonanceDriftNetwork net;
        net.setSeed(0x9A110003u);
        net.prepare(kFsC, ResonanceDriftNetwork::PrepareConfig{});
        isolatePeak(net, kPeak);
        net.setWetGain(0.0f);

        // The criterion is stated at the FR-016 DEFAULTS, so this arm sets
        // neither the depth nor the rate - it REQUIREs them instead.
        REQUIRE(net.isWanderEnabled());
        REQUIRE(net.getPeakPanWander(kPeak) == Catch::Approx(0.2f));
        REQUIRE(net.getWanderRate() == Catch::Approx(0.03f));

        std::size_t clock = 0;
        renderToneStereoAt(net, kFsC, kToneHz, 0.25f, clock, kWindow, nullptr, nullptr);
        clock += kWindow;

        float lowestDb = 0.0f;
        float highestDb = 0.0f;
        bool everyWindowUsable = true;
        for (std::size_t w = 0; w < kWindows; ++w) {
            std::vector<float> left;
            std::vector<float> right;
            renderToneStereoAt(net, kFsC, kToneHz, 0.25f, clock, kWindow, &left, &right);
            clock += kWindow;

            const float rl = rmsOf(left);
            const float rr = rmsOf(right);
            if (!(rl > 0.0f) || !(rr > 0.0f)) {
                everyWindowUsable = false;
                continue;
            }
            const float differenceDb = 20.0f * std::log10(rl / rr);
            if (w == 0) {
                lowestDb = differenceDb;
                highestDb = differenceDb;
            } else {
                lowestDb = std::min(lowestDb, differenceDb);
                highestDb = std::max(highestDb, differenceDb);
            }
        }
        REQUIRE(everyWindowUsable);

        const float rangeDb = highestDb - lowestDb;
        CAPTURE(lowestDb, highestDb, rangeDb, kPanNonVacuityDb);
        REQUIRE(rangeDb > kPanNonVacuityDb);
    }
}

// =============================================================================
// T012 arm set - FR-004's configuration-preserving reset() and FR-046's lighter
// sibling clearAudioState().
// =============================================================================
//
// What each case is FOR:
//   ResetPreservesConfiguration (a) is the only arm anywhere in this phase that
//     fails on the single most likely implementation of FR-004 - forwarding
//     ResonatorBank::reset() and stopping there. That call is a CONFIGURATION
//     WIPE, not a state clear: it leaves every slot at 440 Hz,
//     kDefaultResonatorQ and enabled_[i] = false (resonator_bank.h:225-231), and
//     its own header says "User must reconfigure tuning after calling reset()"
//     (:212). A network that does not re-apply its configuration afterwards
//     renders DIGITAL SILENCE for ever after the first reset(), and EVERY other
//     criterion in this TU still passes against that build.
//   (b) pins the re-apply down from "some audible state" to "the state a freshly
//     configured instance is in".
//   (c) is the FR-051 half: reset() must not disturb a single configuration
//     getter, the seeded pan positions included.
//
//   ClearAudioState (a)/(b)/(c) prove the two methods are BEHAVIOURALLY DISTINCT
//     rather than two names for one effect - which is the whole content of
//     FR-046, since the two bodies differ by exactly one loop. (a) that the audio
//     state really is cleared; (b) that the wander lanes are NOT rewound; (c)
//     that reset()'s lanes ARE. Without (b) and (c) a clearAudioState() that
//     simply called reset() would pass.
// =============================================================================

namespace {

/// The CONFIGURATION half of the read surface (FR-051), and deliberately not
/// SurfaceSnapshot: that aggregate also carries the FR-052 realised-state
/// getters, which reset() is REQUIRED to move (it rewinds the lanes, so every
/// applied frequency jumps back to where the drift started). Comparing the two
/// halves with one struct would force this case either to miss a preserved
/// configuration field or to assert something false about the realised state.
struct ConfigSnapshot {
    std::size_t numPeaks{};
    int anchorMode{};
    float noteHz{};
    float gravity{};
    float wanderRate{};
    float freqSlew{};
    float qSlew{};
    bool wanderEnabled{};
    float mix{};
    float wetGain{};
    std::size_t laneDecimation{};

    std::array<float, ResonanceDriftNetwork::kMaxPeaks> anchorHz{};
    std::array<float, ResonanceDriftNetwork::kMaxPeaks> ratio{};
    std::array<float, ResonanceDriftNetwork::kMaxPeaks> level{};
    std::array<float, ResonanceDriftNetwork::kMaxPeaks> q{};
    std::array<float, ResonanceDriftNetwork::kMaxPeaks> freqWander{};
    std::array<float, ResonanceDriftNetwork::kMaxPeaks> qWander{};
    std::array<float, ResonanceDriftNetwork::kMaxPeaks> gainWander{};
    std::array<float, ResonanceDriftNetwork::kMaxPeaks> pan{};
    std::array<float, ResonanceDriftNetwork::kMaxPeaks> panWander{};
    std::array<float, ResonanceDriftNetwork::kMaxPeaks> wake{};
    std::array<bool, ResonanceDriftNetwork::kMaxPeaks> dormant{};

    [[nodiscard]] bool operator==(const ConfigSnapshot& o) const = default;
};

[[nodiscard]] ConfigSnapshot configSnapshot(const ResonanceDriftNetwork& net) {
    ConfigSnapshot s;
    s.numPeaks = net.getNumPeaks();
    s.anchorMode = static_cast<int>(net.getAnchorMode());
    s.noteHz = net.getNoteFrequency();
    s.gravity = net.getGravity();
    s.wanderRate = net.getWanderRate();
    s.freqSlew = net.getFreqSlewCeiling();
    s.qSlew = net.getQSlewCeiling();
    s.wanderEnabled = net.isWanderEnabled();
    s.mix = net.getMix();
    s.wetGain = net.getWetGain();
    s.laneDecimation = net.getLaneDecimation();
    for (std::size_t i = 0; i < ResonanceDriftNetwork::kMaxPeaks; ++i) {
        s.anchorHz[i] = net.getPeakAnchorHz(i);
        s.ratio[i] = net.getPeakRatio(i);
        s.level[i] = net.getPeakLevel(i);
        s.q[i] = net.getPeakQ(i);
        s.freqWander[i] = net.getFreqWander(i);
        s.qWander[i] = net.getQWander(i);
        s.gainWander[i] = net.getGainWander(i);
        s.pan[i] = net.getPeakPan(i);
        s.panWander[i] = net.getPeakPanWander(i);
        s.wake[i] = net.getPeakWakeAmount(i);
        s.dormant[i] = net.isPeakDormant(i);
    }
    return s;
}

/// A full NON-DEFAULT configuration: every setter FR-004 promises to preserve,
/// driven off its FR-016 default. Called AFTER prepare() (prepare's step 6 is
/// applyDefaults, so a pre-prepare call would be erased) and never followed by
/// setSeed, which redraws the FR-038 pan positions by design (header setSeed).
///
/// mix is deliberately left at its default of 1: at any lower mix the dry path
/// alone carries the (a) arm past -60 dBFS and the arm goes vacuous against the
/// very build it exists to catch.
void applyNonDefaultConfig(ResonanceDriftNetwork& net) {
    using AnchorMode = ResonanceDriftNetwork::AnchorMode;

    // All three modes exercised, ending in the one that reads BOTH the free
    // anchors and the keyed ratios, so a reset() that dropped either is visible.
    net.setAnchorMode(AnchorMode::Free);
    net.setAnchorMode(AnchorMode::Keyed);
    net.setAnchorMode(AnchorMode::Hybrid);
    net.setNoteFrequency(61.0f);
    net.setGravity(0.35f);

    net.setNumPeaks(9);
    // 18 dB, not the 30 dB default. The figure is chosen, not arbitrary: with
    // these levels and Q values the (a) arm's white-noise render lands ~20 dB
    // clear of its -60 dBFS floor, so the arm measures "the re-apply happened"
    // rather than the narrow-band insertion loss of nine high-Q peaks. (The
    // level a resonant bandpass passes from white noise scales with its noise
    // bandwidth f0/Q, and every peak here is 4-13 Hz wide against a 24 kHz
    // Nyquist.)
    net.setWetGain(18.0f);
    net.setWanderRate(0.05f);
    net.setSlewCeilings(0.03f, 0.07f);

    for (std::size_t i = 0; i < ResonanceDriftNetwork::kMaxPeaks; ++i) {
        const auto fi = static_cast<float>(i);
        net.setPeakAnchorHz(i, 37.0f * std::pow(1.41f, fi));  // != the FR-016 table
        net.setPeakRatio(i, 0.75f + (0.37f * fi));
        net.setPeakLevel(i, -8.0f + (0.45f * fi));            // -8 .. -3.05 dB
        net.setPeakQ(i, 8.0f + (4.5f * fi));                  // 8 .. 57.5
        net.setFreqWander(i, 1.0f + (0.25f * fi));
        net.setQWander(i, 0.3f + (0.05f * fi));
        net.setGainWander(i, 2.0f + (0.5f * fi));
        // Never lands on 0.0f, so the (c) comparison never asks Catch::Approx
        // to judge a near-zero against a relative epsilon.
        net.setPeakPan(i, -0.9f + (0.16f * fi));              // -0.9 .. +0.86
        net.setPeakPanWander(i, 0.1f + (0.02f * fi));
        net.setPeakWake(i, 0.5f + (0.04f * fi));              // never 0: see (a)
    }
    // One dormant peak, so the dormancy flag is part of what reset() must keep.
    net.setPeakDormant(4, true);
}

/// Render a whole stereo buffer in 512-sample blocks, overwriting the outputs.
void renderStereoBuffer(ResonanceDriftNetwork& net, const std::vector<float>& inL,
                        const std::vector<float>& inR, std::vector<float>& outL,
                        std::vector<float>& outR) {
    const std::size_t total = inL.size();
    outL.assign(total, 0.0f);
    outR.assign(total, 0.0f);
    for (std::size_t done = 0; done < total;) {
        const std::size_t chunk = std::min(std::size_t{512}, total - done);
        net.processBlock(inL.data() + done, inR.data() + done,
                         outL.data() + done, outR.data() + done, chunk);
        done += chunk;
    }
}

/// Render `numSamples` samples of digital silence, CAPTURING both channels.
/// The tail an uncleared resonator releases is the whole subject of
/// ClearAudioState (a), so the drive has to be silent and the output has to be
/// looked at - renderSilence() above discards it.
void renderSilenceCapture(ResonanceDriftNetwork& net, std::size_t numSamples,
                          std::vector<float>& outL, std::vector<float>& outR) {
    const std::vector<float> zeros(numSamples, 0.0f);
    renderStereoBuffer(net, zeros, zeros, outL, outR);
}

/// FR-016's default wander rate maps to lane decimation 2 (T008's worked table),
/// so a comparison that cleared one instance mid-cycle would shift its lane
/// phase by one control chunk against the twin and measure THAT rather than the
/// lane rewind. Every T012 lane comparison therefore advances a MULTIPLE of the
/// decimation before it touches either instance, so laneCounter_ is already 0
/// and the zeroing inside reset() / clearAudioState() is a genuine no-op.
constexpr std::size_t kLaneAlignedPreChunks = 3000;  // 24 s at kSlowFs, and even

} // namespace

TEST_CASE("ResonanceDriftNetwork_ResetPreservesConfiguration", "[resonance_drift_network]") {
    constexpr std::uint32_t kSeed = 0x0C0FFEE1u;
    // Deliberately NOT a control-chunk multiple: controlPhase_ is mid-grid when
    // reset() runs, so a reset that failed to zero it would desynchronise the
    // 64-sample grid against the fresh reference and (b) would go red.
    constexpr std::size_t kWarmUp = 33777;
    constexpr auto kMeasure = static_cast<std::size_t>(2.0 * kFs);  // 96 000

    std::vector<float> warmL(kWarmUp);
    std::vector<float> warmR(kWarmUp);
    fillNoiseAtDbfs(warmL, 0x51EEDA01u, -12.0f);
    fillNoiseAtDbfs(warmR, 0x51EEDA02u, -12.0f);

    std::vector<float> inL(kMeasure);
    std::vector<float> inR(kMeasure);
    fillNoiseAtDbfs(inL, 0x51EEDB01u, -12.0f);
    fillNoiseAtDbfs(inR, 0x51EEDB02u, -12.0f);

    // The drive is what the arms below say it is, not approximately so.
    const float driveDbL = Krate::DSP::gainToDb(rmsOf(inL));
    const float driveDbR = Krate::DSP::gainToDb(rmsOf(inR));
    CAPTURE(driveDbL, driveDbR);
    REQUIRE(std::abs(driveDbL + 12.0f) <= 0.01f);
    REQUIRE(std::abs(driveDbR + 12.0f) <= 0.01f);

    // ---- the instance under test: configured, RUN, then reset ---------------
    ResonanceDriftNetwork used;
    used.setSeed(kSeed);
    used.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
    applyNonDefaultConfig(used);

    // Preconditions the (a) arm's non-vacuity rests on, MEASURED not assumed.
    REQUIRE(used.getMix() == Catch::Approx(1.0f));  // no dry path to hide behind
    REQUIRE(used.getNumPeaks() == 9u);

    std::vector<float> discardL;
    std::vector<float> discardR;
    renderStereoBuffer(used, warmL, warmR, discardL, discardR);

    const ConfigSnapshot before = configSnapshot(used);
    used.reset();

    std::vector<float> resetL;
    std::vector<float> resetR;
    renderStereoBuffer(used, inL, inR, resetL, resetR);

    SECTION("(a) a reset network still makes sound - the re-apply is real") {
        // THE arm. A build that forwards ResonatorBank::reset() without the
        // FR-014-order re-apply renders digital silence here, because that call
        // left every slot at 440 Hz with enabled_[i] = false.
        REQUIRE(allFinite(resetL));
        REQUIRE(allFinite(resetR));

        const float dbL = Krate::DSP::gainToDb(rmsOf(resetL));
        const float dbR = Krate::DSP::gainToDb(rmsOf(resetR));
        CAPTURE(dbL, dbR);
        REQUIRE(dbL > -60.0f);
        REQUIRE(dbR > -60.0f);
    }

    SECTION("(b) reset() lands on the freshly-configured state, sample-exactly") {
        // The reference is a FRESH instance carrying the same seed and the same
        // setter sequence. It ends with the same reset() the instance under test
        // just took, and that shared call is load-bearing rather than a way of
        // making the comparison easy: this control surface is ASYNCHRONOUS BY
        // DESIGN - setPeakLevel starts a 50 ms LinearRamp (header setPeakLevel),
        // setMix a 20 ms OnePoleSmoother, and an anchor move is slew-limited to
        // freqSlewCeiling octaves per control step (FR-035) - so "prepare plus
        // setters" alone is a state still in motion, and two instances that
        // reached it by different routes could not agree sample-for-sample on
        // any correct build. reset() is precisely the operation that defines the
        // settled state, which is what makes it the honest common basis.
        //
        // What the arm then proves is exactly FR-004's claim: after reset() NO
        // residue of the 33 777-sample render survives - not the biquad states,
        // not the 48 lane positions, not controlPhase_ (mid-grid at the moment
        // of the call), not laneCounter_, not one gate or level ramp.
        ResonanceDriftNetwork fresh;
        fresh.setSeed(kSeed);
        fresh.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
        applyNonDefaultConfig(fresh);
        fresh.reset();

        std::vector<float> freshL;
        std::vector<float> freshR;
        renderStereoBuffer(fresh, inL, inR, freshL, freshR);

        // Non-vacuity FIRST: two silent renders agree perfectly.
        REQUIRE(peakAbs(freshL) > 0.0f);
        REQUIRE(peakAbs(freshR) > 0.0f);

        const float diffL = maxAbsDiff(resetL, freshL);
        const float diffR = maxAbsDiff(resetR, freshR);
        CAPTURE(diffL, diffR);
        REQUIRE(diffL == 0.0f);
        REQUIRE(diffR == 0.0f);
    }

    SECTION("(c) every configuration getter survives reset() unchanged") {
        const ConfigSnapshot after = configSnapshot(used);
        REQUIRE(after == before);

        // Non-vacuity: the snapshot really did carry the non-default values, so
        // "unchanged" is not a statement about twelve copies of the FR-016 table.
        REQUIRE(before.numPeaks == 9u);
        REQUIRE(before.anchorMode
                == static_cast<int>(ResonanceDriftNetwork::AnchorMode::Hybrid));
        REQUIRE(before.gravity == Catch::Approx(0.35f));
        REQUIRE(before.noteHz == Catch::Approx(61.0f));
        REQUIRE(before.wetGain == Catch::Approx(18.0f));
        REQUIRE(before.dormant[4]);

        // getPeakPan is named explicitly by the criterion: the FR-038 positions
        // are CONFIGURATION, drawn only by setSeed, and reset() must not redraw
        // them (header reset()). An absolute margin rather than Catch::Approx,
        // because the setter and this line evaluate the same expression in two
        // translation-unit positions and /fp:fast may contract one of them.
        bool pansAreConfigured = true;
        for (std::size_t i = 0; i < ResonanceDriftNetwork::kMaxPeaks; ++i) {
            const float expected = -0.9f + (0.16f * static_cast<float>(i));
            if (std::abs(before.pan[i] - expected) > 1.0e-5f) {
                pansAreConfigured = false;
            }
        }
        REQUIRE(pansAreConfigured);
    }
}

TEST_CASE("ResonanceDriftNetwork_ClearAudioState", "[resonance_drift_network]") {
    SECTION("(a) a Q = 100 ring is gone on the very next sample") {
        // RT60 = Q * ln1000 / (pi * f) = 100 * 6.9078 / (pi * 40) = 5.50 s, so an
        // uncleared resonator is still near full amplitude 64 samples later. The
        // twin is what makes that concrete rather than merely asserted.
        constexpr std::size_t kPeak = 0;
        constexpr float kToneHz = 40.0f;  // peak 0's FR-016 anchor
        constexpr auto kDrive = static_cast<std::size_t>(1.0 * kFs);
        constexpr std::size_t kTail = 64;

        const auto configure = [&](ResonanceDriftNetwork& net) {
            net.setSeed(0xC1EA5E01u);
            net.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
            isolatePeak(net, kPeak);
            net.setPeakQ(kPeak, 100.0f);
            // Wander off so the resonator sits exactly on the drive frequency:
            // this arm is about STORED ENERGY, not about where the peak drifted.
            net.setWanderEnabled(false);
            // A unity trim keeps the ring in a range where "below
            // kSampleTolerance" is a statement about the clear rather than about
            // FR-018's output clamp.
            net.setWetGain(0.0f);
        };

        ResonanceDriftNetwork cleared;
        ResonanceDriftNetwork twin;
        configure(cleared);
        configure(twin);

        // Both instances see the SAME phase-continuous tone from sample 0, so
        // the only thing that can separate their tails is the clear itself.
        constexpr std::size_t kStart = 0;
        renderToneStereoAt(cleared, kFs, kToneHz, 0.25f, kStart, kDrive, nullptr, nullptr);
        renderToneStereoAt(twin, kFs, kToneHz, 0.25f, kStart, kDrive, nullptr, nullptr);

        // The preconditions, measured AFTER the drive rather than transcribed -
        // and after deliberately: setPeakQ writes the BASE Q, and the applied Q
        // climbs to it under FR-035's ceiling (log2(100/12) = 3.06 octaves at
        // 0.05 octaves per control step = 61 steps ~ 81 ms), so reading either
        // getter before the render would report the Q = 12 the peak started at.
        const float appliedQ = cleared.getPeakCurrentQ(kPeak);
        const float rt60Seconds = cleared.getPeakEquivalentRt60(kPeak);
        CAPTURE(appliedQ, rt60Seconds);
        REQUIRE(appliedQ == Catch::Approx(100.0f).margin(0.5f));
        REQUIRE(rt60Seconds > 5.0f);  // 100 * ln1000 / (pi * 40) = 5.50 s

        cleared.clearAudioState();

        std::vector<float> clearedL;
        std::vector<float> clearedR;
        std::vector<float> twinL;
        std::vector<float> twinR;
        renderSilenceCapture(cleared, kTail, clearedL, clearedR);
        renderSilenceCapture(twin, kTail, twinL, twinR);

        // NON-VACUITY: without the clear there is a large tail to remove.
        const float twinTail = std::max(peakAbs(twinL), peakAbs(twinR));
        CAPTURE(twinTail);
        REQUIRE(twinTail > 1.0e-3f);

        // The criterion, on the VERY NEXT sample - and, for free, across the
        // whole 64-sample tail, which a clear that merely retriggered a fade
        // would miss.
        CAPTURE(clearedL[0], clearedR[0]);
        REQUIRE(std::abs(clearedL[0]) < Krate::DSP::TestUtils::kSampleTolerance);
        REQUIRE(std::abs(clearedR[0]) < Krate::DSP::TestUtils::kSampleTolerance);
        REQUIRE(peakAbs(clearedL) < Krate::DSP::TestUtils::kSampleTolerance);
        REQUIRE(peakAbs(clearedR) < Krate::DSP::TestUtils::kSampleTolerance);
    }

    SECTION("(b) clearAudioState() does NOT rewind the lanes") {
        // Sixty seconds of comparison, at the lowest rate prepare() accepts. The
        // lane properties in question are defined in SECONDS and FR-037's
        // decimation is sample-rate independent (the T008 arms establish both),
        // so 8 kHz is the honest rate for a 60 s window.
        constexpr std::uint32_t kSeed = 0xC1EA5E02u;
        constexpr std::size_t kCompareSteps = 60u * kSlowStepsPerSecond;  // 7500

        ResonanceDriftNetwork cleared;
        ResonanceDriftNetwork twin;
        cleared.setSeed(kSeed);
        twin.setSeed(kSeed);
        cleared.prepare(kSlowFs, ResonanceDriftNetwork::PrepareConfig{});
        twin.prepare(kSlowFs, ResonanceDriftNetwork::PrepareConfig{});

        // The arm is stated at the FR-016 defaults, so it REQUIREs them.
        REQUIRE(cleared.isWanderEnabled());
        REQUIRE(cleared.getWanderRate() == Catch::Approx(0.03f));
        REQUIRE(cleared.getLaneDecimation() == 2u);
        static_assert((kLaneAlignedPreChunks % 2u) == 0u,
                      "the pre-advance must leave laneCounter_ at 0");

        advanceControlSteps(cleared, kLaneAlignedPreChunks);
        advanceControlSteps(twin, kLaneAlignedPreChunks);

        // NON-VACUITY of the pre-advance: the lanes have actually gone somewhere,
        // so "agrees with the twin" is not a comparison of two untouched states.
        float preExcursion = 0.0f;
        for (std::size_t i = 0; i < kNumPeaks; ++i) {
            const float moved = std::abs(std::log2(twin.getPeakCurrentFrequency(i))
                                         - std::log2(twin.getPeakAnchorHz(i)));
            preExcursion = std::max(preExcursion, moved);
        }
        CAPTURE(preExcursion);
        REQUIRE(preExcursion > 1.0e-3f);

        cleared.clearAudioState();

        const auto tol = static_cast<float>(Krate::DSP::TestUtils::kMetricTolerance);
        float worstHz = 0.0f;
        float worstQ = 0.0f;
        float worstGainDb = 0.0f;
        float worstPan = 0.0f;
        for (std::size_t s = 0; s < kCompareSteps; ++s) {
            advanceControlSteps(cleared, 1);
            advanceControlSteps(twin, 1);
            for (std::size_t i = 0; i < kNumPeaks; ++i) {
                worstHz = std::max(worstHz,
                                   std::abs(cleared.getPeakCurrentFrequency(i)
                                            - twin.getPeakCurrentFrequency(i)));
                worstQ = std::max(worstQ, std::abs(cleared.getPeakCurrentQ(i)
                                                   - twin.getPeakCurrentQ(i)));
                worstGainDb = std::max(worstGainDb,
                                       std::abs(cleared.getPeakCurrentGainDb(i)
                                                - twin.getPeakCurrentGainDb(i)));
                worstPan = std::max(worstPan, std::abs(cleared.getPeakCurrentPan(i)
                                                       - twin.getPeakCurrentPan(i)));
            }
        }
        CAPTURE(worstHz, worstQ, worstGainDb, worstPan, tol);
        REQUIRE(worstHz <= tol);
        REQUIRE(worstQ <= tol);
        REQUIRE(worstGainDb <= tol);
        REQUIRE(worstPan <= tol);
    }

    SECTION("(c) reset() DOES rewind them - the two methods are distinct") {
        // Identical setup to (b), with reset() substituted for clearAudioState().
        // The one loop of difference between the two bodies has to show up as a
        // measurable divergence, or FR-046 is describing a method that does not
        // exist.
        constexpr std::uint32_t kSeed = 0xC1EA5E02u;  // the same seed as (b)
        constexpr std::size_t kCompareSteps = 60u * kSlowStepsPerSecond;

        ResonanceDriftNetwork rewound;
        ResonanceDriftNetwork twin;
        rewound.setSeed(kSeed);
        twin.setSeed(kSeed);
        rewound.prepare(kSlowFs, ResonanceDriftNetwork::PrepareConfig{});
        twin.prepare(kSlowFs, ResonanceDriftNetwork::PrepareConfig{});

        advanceControlSteps(rewound, kLaneAlignedPreChunks);
        advanceControlSteps(twin, kLaneAlignedPreChunks);

        rewound.reset();

        double accumulated = 0.0;
        std::size_t samples = 0;
        for (std::size_t s = 0; s < kCompareSteps; ++s) {
            advanceControlSteps(rewound, 1);
            advanceControlSteps(twin, 1);
            for (std::size_t i = 0; i < kNumPeaks; ++i) {
                const float a = rewound.getPeakCurrentFrequency(i);
                const float b = twin.getPeakCurrentFrequency(i);
                if (a <= 0.0f || b <= 0.0f) continue;
                accumulated += std::abs(static_cast<double>(std::log2(a))
                                        - static_cast<double>(std::log2(b)));
                ++samples;
            }
        }
        REQUIRE(samples > 0u);
        const double meanAbsLog2 = accumulated / static_cast<double>(samples);

        // Ten times (b)'s tolerance. (b) measures agreement to a few ULP; a
        // rewound lane set differs by a fair fraction of the FR-016 default
        // +/- 3 semitone excursion, i.e. by ~1e-2 octaves and up.
        const double bound = 10.0 * Krate::DSP::TestUtils::kMetricTolerance;
        CAPTURE(meanAbsLog2, bound);
        REQUIRE(meanAbsLog2 > bound);
    }
}


// =============================================================================
// T013 arm set - FR-018's output clamp and its SATURATING engagement counter
// =============================================================================
// Why this case exists at all: every OTHER criterion that reads
// getClampEngagementCount() requires it to be zero or unchanged (SC-001 (b),
// SC-008 (d), SC-009 (c), SC-016 (d)), and nothing anywhere else in this phase
// drives the output past +/- kOutputClamp. A build with NO clamp and the getter
// hardcoded to 0 passes the entire rest of the spec. This case fails on BOTH
// halves of that build: the bound (a missing clamp) and the increment (a
// counter that cannot count).

TEST_CASE("ResonanceDriftNetwork_OutputClampEngages", "[resonance_drift_network]") {
    // Peak 6's FR-016 default FREE anchor is 265 Hz (the kDefaultAnchorHz table,
    // resonance_drift_network.h:861-863), so driving the network exactly on that
    // peak's resonance needs no setPeakAnchorHz call: the arm below is the
    // shipped default patch plus three IN-SPEC ceilings (max Q, max peak level,
    // max wet trim) and the dormancy of the other eleven peaks. Nothing here is
    // an out-of-range injection - FR-018's clamp has to be reachable from the
    // control surface, or it is protecting against nothing a host can do.
    constexpr std::size_t kAnchorPeak = 6;
    constexpr float kAnchorHz = 265.0f;
    constexpr float kClamp = ResonanceDriftNetwork::kOutputClamp;
    constexpr std::size_t kMaxPeaks = ResonanceDriftNetwork::kMaxPeaks;
    constexpr std::size_t kDriveSamples = static_cast<std::size_t>(5.0 * kFs);  // 240 000
    constexpr std::size_t kBlock = 512;

    ResonanceDriftNetwork net;
    net.setSeed(0x0C1A3D01u);
    net.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});

    // The three FR-016 defaults this arm departs from, READ BACK rather than
    // hardcoded - part (3) restores exactly these, and reading them keeps the
    // restore honest if the default table ever moves.
    const float defaultQ = net.getPeakQ(kAnchorPeak);
    const float defaultLevelDb = net.getPeakLevel(kAnchorPeak);
    const float defaultWetGainDb = net.getWetGain();
    REQUIRE(net.getPeakAnchorHz(kAnchorPeak) == Catch::Approx(kAnchorHz));
    REQUIRE(net.getClampEngagementCount() == 0u);

    // ---- (1) the in-spec drive ---------------------------------------------
    for (std::size_t i = 0; i < kMaxPeaks; ++i) {
        net.setPeakDormant(i, i != kAnchorPeak);  // one peak awake, eleven asleep
    }
    // Wander OFF pins the awake peak ON its 265 Hz anchor for the whole render
    // (FR-036's depths collapse to zero), which is both the strongest in-spec
    // drive and a deterministic one - a wandering peak would spend part of the
    // render up to +/- 3 semitones off a 2.65 Hz-wide resonance.
    net.setWanderEnabled(false);
    net.setPeakQ(kAnchorPeak, 100.0f);     // kMaxResonatorQ
    net.setPeakLevel(kAnchorPeak, 12.0f);  // FR-017's ceiling
    net.setWetGain(48.0f);                 // FR-045's ceiling
    net.setMix(1.0f);
    REQUIRE(net.getPeakQ(kAnchorPeak) == Catch::Approx(100.0f));
    REQUIRE(net.getPeakLevel(kAnchorPeak) == Catch::Approx(12.0f));
    REQUIRE(net.getWetGain() == Catch::Approx(48.0f));

    // 0 dBFS sine at the anchor, identical on both channels. Phase is
    // accumulated in DOUBLE: at 240 000 samples a float phase argument loses
    // enough of the low bits to walk off a 2.65 Hz-wide resonance.
    std::vector<float> inL(kDriveSamples);
    std::vector<float> inR(kDriveSamples);
    const double omega =
        2.0 * static_cast<double>(Krate::DSP::kPi) * static_cast<double>(kAnchorHz) / kFs;
    for (std::size_t n = 0; n < kDriveSamples; ++n) {
        inL[n] = static_cast<float>(std::sin(omega * static_cast<double>(n)));
        inR[n] = inL[n];
    }
    const float drivePeak = peakAbs(inL);
    CAPTURE(drivePeak);
    REQUIRE(drivePeak == Catch::Approx(1.0f).margin(0.01f));  // 0 dBFS, and it means it

    std::vector<float> outL(kDriveSamples, 0.0f);
    std::vector<float> outR(kDriveSamples, 0.0f);
    for (std::size_t done = 0; done < kDriveSamples;) {
        const std::size_t chunk = std::min(kBlock, kDriveSamples - done);
        net.processBlock(inL.data() + done, inR.data() + done,
                         outL.data() + done, outR.data() + done, chunk);
        done += chunk;
    }
    REQUIRE(allFinite(outL));
    REQUIRE(allFinite(outR));

    // ---- (2) BOTH halves, and the case is only real with both --------------
    // (2a) the bound holds on every sample of both channels. Scanned rather than
    //      REQUIREd per sample: 240 000 samples would otherwise register 480 000
    //      assertions and dominate the suite.
    const float peakL = peakAbs(outL);
    const float peakR = peakAbs(outR);
    CAPTURE(peakL, peakR, kClamp);
    REQUIRE(peakL <= kClamp);
    REQUIRE(peakR <= kClamp);

    // (2b) ...and the drive REACHED the bound, so (2a) is not vacuous: a clamped
    //      sample is exactly kOutputClamp and flushDenormal leaves it alone.
    REQUIRE(std::max(peakL, peakR) == Catch::Approx(kClamp));

    // (2c) ...and the counter can actually increment. THIS is the half that
    //      fails on a hardcoded-zero getClampEngagementCount(), which every
    //      other criterion in the phase would happily accept.
    const std::uint32_t engaged = net.getClampEngagementCount();
    CAPTURE(engaged);
    REQUIRE(engaged > 0u);

    // ---- (3) an in-spec DEFAULT patch does not engage the clamp -------------
    // The same instance, restored through the setters. prepare() is the only
    // path back to the FR-016 defaults and it ZEROES the counter
    // (resonance_drift_network.h:311), so a re-prepare would turn "unchanged"
    // into "still zero" - a strictly weaker assertion. This part is what makes
    // SC-001 (b)'s `== 0` a real statement about the default configuration.
    for (std::size_t i = 0; i < kMaxPeaks; ++i) {
        net.setPeakDormant(i, false);
    }
    net.setWanderEnabled(true);
    net.setPeakQ(kAnchorPeak, defaultQ);
    net.setPeakLevel(kAnchorPeak, defaultLevelDb);
    net.setWetGain(defaultWetGainDb);
    net.setMix(1.0f);

    // The crossover out of (1) is deliberately NOT inside the measured window:
    // the FR-041 ramps take 50 ms and the Q = 100 ring at 265 Hz decays with a
    // ~120 ms time constant, so the first samples after the restore are still
    // (1)'s gains applied to (1)'s stored energy and clamp legitimately. One
    // second of settle is ~20 ramp lengths and ~8 ring time constants.
    constexpr std::size_t kSettleSamples = static_cast<std::size_t>(1.0 * kFs);
    constexpr std::size_t kNoiseSamples = static_cast<std::size_t>(5.0 * kFs);
    constexpr std::size_t kTotalNoise = kSettleSamples + kNoiseSamples;

    std::vector<float> noiseL(kTotalNoise);
    std::vector<float> noiseR(kTotalNoise);
    fillNoiseAtDbfs(noiseL, 0x2468ACE0u, -12.0f);
    fillNoiseAtDbfs(noiseR, 0x13579BDFu, -12.0f);
    const float driveDbL = Krate::DSP::gainToDb(rmsOf(noiseL));
    const float driveDbR = Krate::DSP::gainToDb(rmsOf(noiseR));
    CAPTURE(driveDbL, driveDbR);
    REQUIRE(std::abs(driveDbL + 12.0f) <= 0.01f);
    REQUIRE(std::abs(driveDbR + 12.0f) <= 0.01f);

    std::vector<float> noiseOutL(kTotalNoise, 0.0f);
    std::vector<float> noiseOutR(kTotalNoise, 0.0f);
    for (std::size_t done = 0; done < kSettleSamples;) {
        const std::size_t chunk = std::min(kBlock, kSettleSamples - done);
        net.processBlock(noiseL.data() + done, noiseR.data() + done,
                         noiseOutL.data() + done, noiseOutR.data() + done, chunk);
        done += chunk;
    }

    // The baseline for the measured window. It is >= (1)'s count and NON-ZERO,
    // so "unchanged" below is a statement about a live counter rather than about
    // a zero a broken build reports just as happily.
    const std::uint32_t settled = net.getClampEngagementCount();
    CAPTURE(settled);
    REQUIRE(settled >= engaged);
    REQUIRE(settled > 0u);

    for (std::size_t done = kSettleSamples; done < kTotalNoise;) {
        const std::size_t chunk = std::min(kBlock, kTotalNoise - done);
        net.processBlock(noiseL.data() + done, noiseR.data() + done,
                         noiseOutL.data() + done, noiseOutR.data() + done, chunk);
        done += chunk;
    }

    // The measured window only: the settle samples above are excluded from the
    // peak scan for the same reason they are excluded from the count.
    const std::vector<float> measuredL(
        noiseOutL.begin() + static_cast<std::ptrdiff_t>(kSettleSamples), noiseOutL.end());
    const std::vector<float> measuredR(
        noiseOutR.begin() + static_cast<std::ptrdiff_t>(kSettleSamples), noiseOutR.end());
    const std::uint32_t afterDefaults = net.getClampEngagementCount();
    const float defaultPeakL = peakAbs(measuredL);
    const float defaultPeakR = peakAbs(measuredR);
    CAPTURE(afterDefaults, defaultPeakL, defaultPeakR);
    REQUIRE(allFinite(measuredL));
    REQUIRE(allFinite(measuredR));
    REQUIRE(defaultPeakL < kClamp);
    REQUIRE(defaultPeakR < kClamp);
    REQUIRE(afterDefaults == settled);
}

// =============================================================================
// T015 arm set - SC-017: REALISED Q vs REPORTED Q (FR-014, FR-015, FR-031,
//                        FR-032)
// =============================================================================
// THE ONLY CRITERION IN THIS PHASE THAT CAN FAIL ON THE WRITE-ORDER TRAP.
//
// ResonatorBank::setFrequency re-derives
//   qValues_[index] = rt60ToQ(frequencies_[index], decays_[index])
// (resonator_bank.h:333) off a decay table this component NEVER writes
// (ResonatorBank::setDecay is called from nowhere in
// resonance_drift_network.h), so decays_[i] stands at kDefaultDecayTime = 1.0 s
// forever. Two mutations of the FR-014/FR-015 write path therefore leave the
// FILTER at rt60ToQ(f, 1.0) = pi * f / kLn1000 = 0.4548 * f, clamped to
// [kMinResonatorQ, kMaxResonatorQ] = [0.1, 100] (resonator_bank.h:92-99):
//
//   (i)  writing Q BEFORE frequency - the frequency write discards the Q;
//   (ii) letting FR-015's change detection skip an "unchanged" Q write after a
//        frequency write.
//
// Both are SILENT. The audio still sounds plausible, and getPeakCurrentQ() is
// unmoved because FR-052 makes it the NETWORK's own applied value
// (resonance_drift_network.h:784-786), not a read of the filter. Every other Q
// criterion in this phase - SC-003 (c), SC-014 - reads that same echo, and
// SC-001 (d)'s ring-out is an upper bound that passes with a SHORTER decay. So
// this case measures the bandwidth the filter actually realises.
//
// -----------------------------------------------------------------------------
// ESTIMATOR: the Phase 2 Welch-on-an-arbitrary-probe-grid band-ratio fit,
// copied from noise_organism_test.cpp:1631-1728 (welchPowerGrid +
// measuredBandSum / modelBandSum + fitQFromBandRatio bisection).
//
// NOT a -3 dB crossing search: ~1 dB of noise on a Lorentzian flank
// (-4.34 dB per half-width) is ~23 % of the half-width, i.e. the whole +/-25 %
// budget consumed before any real defect. The band integrals average over ~80-97
// probe frequencies and are two orders of magnitude quieter. The quantity is the
// same quantity - f0 / Q of the best-fitting resonance.
//
// The fit inverts the analog band-pass prototype ResonatorBank realises,
// |H(f)|^2 = 1 / (1 + Q^2 (f/f0 - f0/f)^2) (resonator_bank.h:560-591), from the
// ratio of a NARROW to a WIDE band integral. Because modelBandSum sums the
// analytic response over the SAME probe grid, the grid's discretisation cancels
// in the ratio; what does NOT cancel is the Welch window's own width, which is
// why every window below is sized at 0.2 * BW (see kWindowWidthFraction).
//
// -----------------------------------------------------------------------------
// FIXTURE SIZING, DERIVED RATHER THAN GUESSED. Two independent error terms:
//
//  * BIAS (Voigt broadening). The Welch estimate is the true response convolved
//    with the window's power spectrum, whose -3 dB width is 1.44 * fs / W.
//    Sizing that at 0.2 * BW costs ~4 % in the fitted Q - the figure Phase 2
//    measured for exactly this estimator
//    (noise_organism_test.cpp:1601-1605). Hence W = (1.44 / 0.2) * fs / BW.
//  * VARIANCE. A resonance of bandwidth BW observed for T seconds carries ~BW*T
//    independent samples, so sigma(ln Q) ~ c / sqrt(BW*T) - error-propagated
//    through the two correlated band sums as c ~ 0.65 at Q >= 12 and c ~ 0.92 at
//    Q = 2 (the low-Q geometry is capped by wideHalf <= 0.75 * f0, which raises
//    the narrow/wide correlation and lowers the sensitivity
//    dln(ratio)/dln(Q)). `frames` below is chosen per combination for
//    sigma <= ~5 %, i.e. at least 3 sigma of headroom inside SC-017's 25 % once
//    the 4 % bias is paid.
//
// COST. Welch is O(frames * probes * W) = O(2 * probes * totalSamples), which is
// why the 40 Hz / Q = 100 combination (BW = 0.4 Hz, so 441 s of audio at the
// 8 kHz floor) dominates the whole case at ~0.7 G inner iterations. The case
// runs in a few seconds and is deliberately NOT tagged [long]: it is the only
// criterion that can fail on the write-order trap, so it belongs in the
// per-push lane.
//
// SAMPLE RATE per anchor, chosen so the digital response stays close to the
// analog prototype the model assumes: 8 kHz (= kMinUsableSampleRate, the
// cheapest legal rate, which prepare floors to) for the 40 Hz and 265 Hz
// anchors, 16 kHz for 1278 Hz. At f0/fs = 0.08 the bilinear warping of the -3 dB
// width is tan(w/2)/(w/2) = 1.021, i.e. ~2 %; at 1278 Hz on an 8 kHz rate it
// would be ~9 %, a third of the budget spent on the harness.
//
// -----------------------------------------------------------------------------
// WHY THE FIXTURE MOVES THE ANCHOR AFTER SETTING Q. Mutation (ii) only bites
// when a frequency write happens with Q unchanged. Setting Q first and THEN
// walking the anchor an octave (which the FR-035 slew ceiling spreads over ~50
// control steps, each one a setFrequency) is what puts the trap on the path; a
// fixture that configured the anchor first and never moved it again would be
// green against both mutations. This is also SC-017's "frequency written before
// the Q read" clause.
//
// -----------------------------------------------------------------------------
// INJECTION ARM (MANDATORY, spec.md SC-017; run by hand, both mutations must go
// RED before this case is trusted). The two sites, both in
// dsp/include/krate/dsp/systems/resonance_drift_network.h:
//
//   Site A - updateControl()'s indivisible write pair, :1560-1565
//              if (freqOrQMoved) {
//                  bankSetFrequency(i, p.appliedHz);  // FIRST
//                  bankSetQ(i, p.appliedQ);           // ALWAYS immediately after
//   Site B - bankApply()'s unconditional FR-014 triple, :1059-1067
//
//   Mutation (i)  - swap the two calls at Site A (and, for a complete injection,
//                   at Site B) so Q is written BEFORE frequency.
//   Mutation (ii) - guard Site A's Q write with change detection:
//                       if (p.appliedQ != p.lastWrittenQ) bankSetQ(i, p.appliedQ);
//
//   Mutating Site A alone is sufficient for BOTH: the settling walk's last write
//   comes from updateControl, and nothing rewrites the slot afterwards.
//
//   PREDICTED READING under either mutation, per anchor - rt60ToQ(f, 1.0):
//       40 Hz  -> 18.19          (clean: resolvable at every Q in the grid)
//      265 Hz  -> 120.5  -> 100  (clamped by kMaxResonatorQ)
//     1278 Hz  -> 581.0  -> 100  (clamped)
//   SEVEN of the nine combinations therefore go red. That the other two CANNOT
//   is a property of the clamp, not an oversight, and is recorded here so a
//   later reader does not delete them as redundant: (265 Hz, Q = 100) and
//   (1278 Hz, Q = 100) configure exactly the Q the clamp already produces.
//   THE READING TO RECORD is the (40 Hz, Q = 12) combination: its band geometry
//   is scaled to BW = 3.33 Hz and resolves the predicted 18.19 without
//   saturating, whereas a combination whose geometry was built for a very
//   different Q (e.g. 40 Hz / Q = 2, whose wide band is 30 Hz) can only report
//   "far outside the band geometry" once the realised resonance collapses inside
//   its own narrow band.
//
//   RUN, AND MEASURED - 2026-09-11, MSVC Release, both mutations built and
//   executed, then reverted and re-verified. Realised f / BW at the nine grid
//   points, in the case's own order (40 / 265 / 1278 Hz x Q = 2 / 12 / 100):
//
//     unmutated      2.090  11.877  99.052 | 2.070  11.845  97.870 | 2.280  12.289  111.074
//     mutation (i)  20.526  18.045  18.762 | 74.308 91.409  97.870 | 88.792 89.116  111.074
//     mutation (ii) 20.526  18.045  18.762 | 74.308 91.409  97.870 | 88.792 89.116  111.074
//
//   Verdicts: unmutated 192/192 assertions PASSED; mutation (i) 14 of 192
//   FAILED; mutation (ii) 14 of 192 FAILED - seven combinations x two CHECKs
//   each, exactly the seven predicted. The (40 Hz, Q = 12) reading is 18.045
//   against the predicted rt60ToQ(40, 1.0) = 18.19, and the 265/1278 Hz rows
//   saturate at the kMaxResonatorQ clamp as predicted. The two that stay green
//   under mutation are (265 Hz, Q = 100) at 97.870 and (1278 Hz, Q = 100) at
//   111.074 - the clamp's own value, as recorded above.
//
//   The two mutations reading IDENTICALLY is expected and is itself evidence:
//   both make the last write to the slot a stale-Q one, so they converge on the
//   same realised filter. The readings are transcribed in compliance.md (T024).
//
// All nine agreement assertions are CHECK, not REQUIRE, so a mutated build
// reports every combination in one run instead of aborting on the first.
// `dsp_systems_tests.exe "ResonanceDriftNetwork_RealisedQ" --success` prints the
// CAPTUREd readings on a green build too.
// =============================================================================
namespace {

/// pi in double. Krate::DSP::kPi is a FLOAT constant (math_constants.h:28) and
/// the incremental rotation below runs for up to 144 000 samples per window; a
/// float-precision angle is not what this estimator should be built on.
constexpr double kTwoPiExact = 6.283185307179586476925286766559;

/// Welch power estimate on an ARBITRARY probe grid (no FFT: the grid a 0.4 Hz
/// resonance needs is finer than any FFT whose window fits inside the record).
/// Hann-windowed, `hop`-overlapped, evaluated by an incremental complex rotation
/// in double precision, re-normalised every 4096 samples so the rotation cannot
/// drift over a long window. Copied from noise_organism_test.cpp:1630-1679.
[[nodiscard]] std::vector<double> welchPowerGrid(const std::vector<float>& x, double sampleRate,
                                                 const std::vector<double>& probeHz,
                                                 std::size_t windowLen, std::size_t hop) {
    std::vector<double> power(probeHz.size(), 0.0);
    if (windowLen == 0 || hop == 0 || x.size() < windowLen) {
        return power;
    }

    std::vector<double> hann(windowLen);
    for (std::size_t i = 0; i < windowLen; ++i) {
        hann[i] = 0.5 - 0.5 * std::cos(kTwoPiExact * static_cast<double>(i) /
                                       static_cast<double>(windowLen));
    }

    std::vector<double> frame(windowLen);
    std::size_t         frames = 0;
    for (std::size_t start = 0; start + windowLen <= x.size(); start += hop) {
        for (std::size_t i = 0; i < windowLen; ++i) {
            frame[i] = static_cast<double>(x[start + i]) * hann[i];
        }
        for (std::size_t p = 0; p < probeHz.size(); ++p) {
            const double omega = kTwoPiExact * probeHz[p] / sampleRate;
            const double stepC = std::cos(omega);
            const double stepS = std::sin(omega);
            double       c     = 1.0;
            double       s     = 0.0;
            double       re    = 0.0;
            double       im    = 0.0;
            for (std::size_t i = 0; i < windowLen; ++i) {
                re += frame[i] * c;
                im -= frame[i] * s;
                const double nextC = c * stepC - s * stepS;
                s                  = s * stepC + c * stepS;
                c                  = nextC;
                if ((i % 4096) == 4095) {
                    const double inverse = 1.0 / std::sqrt(c * c + s * s);
                    c *= inverse;
                    s *= inverse;
                }
            }
            power[p] += re * re + im * im;
        }
        ++frames;
    }
    if (frames > 0) {
        for (double& v : power) {
            v /= static_cast<double>(frames);
        }
    }
    return power;
}

/// Sum of `power` over the probes within +/- halfWidth of f0. The grid spacing
/// is a common factor of every ratio taken from these sums, so it is omitted.
/// Copied from noise_organism_test.cpp:1682-1693.
[[nodiscard]] double measuredBandSum(const std::vector<double>& power,
                                     const std::vector<double>& probeHz, double f0,
                                     double halfWidth) {
    double total = 0.0;
    for (std::size_t i = 0; i < probeHz.size(); ++i) {
        if (std::fabs(probeHz[i] - f0) <= halfWidth + 1.0e-9) {
            total += power[i];
        }
    }
    return total;
}

/// The same sum, over the ANALYTIC band-pass power response and the SAME grid,
/// so the discretisation of the two sides cancels in the ratio.
/// Copied from noise_organism_test.cpp:1695-1710.
[[nodiscard]] double modelBandSum(double q, const std::vector<double>& probeHz, double f0,
                                  double halfWidth) {
    double total = 0.0;
    for (const double f : probeHz) {
        if (f <= 0.0 || std::fabs(f - f0) > halfWidth + 1.0e-9) {
            continue;
        }
        const double detune = f / f0 - f0 / f;
        total += 1.0 / (1.0 + q * q * detune * detune);
    }
    return total;
}

/// The bisection bracket. Wider than Phase 2's [0.5, 200] at BOTH ends so a
/// mutated build's reading is REPORTED rather than saturated against the bracket
/// - the injection arm has to record a number, and the shipped grid already
/// spans Q = 2 to Q = 100.
constexpr double kFitQLow  = 0.25;
constexpr double kFitQHigh = 400.0;

/// Recover Q from the measured narrow/wide band-energy ratio by bisection.
/// The ratio rises monotonically with Q (a narrower resonance puts a larger
/// share of its energy inside the narrow band), so bisection is well posed.
/// Copied from noise_organism_test.cpp:1712-1728.
[[nodiscard]] double fitQFromBandRatio(double measuredRatio, const std::vector<double>& probeHz,
                                       double f0, double narrowHalf, double wideHalf) {
    double low  = kFitQLow;
    double high = kFitQHigh;
    for (int iteration = 0; iteration < 100; ++iteration) {
        const double mid = 0.5 * (low + high);
        const double ratio =
            modelBandSum(mid, probeHz, f0, narrowHalf) / modelBandSum(mid, probeHz, f0, wideHalf);
        if (ratio < measuredRatio) {
            low = mid;
        } else {
            high = mid;
        }
    }
    return 0.5 * (low + high);
}

/// The Welch window's -3 dB width as a fraction of the resonance bandwidth. 0.2
/// buys ~4 % of Voigt broadening (Phase 2's measured figure for this estimator);
/// the window is 1.44 * fs / (kWindowWidthFraction * BW) samples long.
constexpr double kWindowWidthFraction = 0.2;
constexpr double kWindowLengthFactor  = 1.44 / kWindowWidthFraction;  // 7.2

/// SC-017's grid: peakQ in {2, 12, 100} x anchors {40, 265, 1278} Hz - the
/// FR-016 default table's bottom, middle and top entries. Ordered
/// anchor-major, Q-ascending; the RT60 arm at the end of the case indexes it as
/// [caseIndex / 3][caseIndex % 3] and depends on that ordering.
struct RealisedQCase {
    double      anchorHz;
    float       peakQ;
    double      sampleRate;
    std::size_t frames;  ///< Welch averages; see the sizing note above.
};

constexpr std::array<RealisedQCase, 9> kRealisedQCases{{
    // anchor      Q       fs      frames   (sigma(lnQ), Welch cost)
    {40.0, 2.0f, 8000.0, 128},     // ~4.3 %,  30 M
    {40.0, 12.0f, 8000.0, 64},     // ~4.3 %, 109 M
    {40.0, 100.0f, 8000.0, 48},    // ~4.9 %, 684 M  <- dominates the case
    {265.0, 2.0f, 8000.0, 128},    // ~4.3 %,   5 M
    {265.0, 12.0f, 8000.0, 128},   // ~3.1 %,  33 M
    {265.0, 100.0f, 8000.0, 48},   // ~4.9 %, 102 M
    {1278.0, 2.0f, 16000.0, 128},  // ~4.1 %,   2 M
    {1278.0, 12.0f, 16000.0, 128}, // ~3.0 %,  14 M
    {1278.0, 100.0f, 16000.0, 64}, // ~4.5 %,  56 M
}};

/// The narrow/wide band geometry for one combination, scaled to the EXPECTED
/// bandwidth so every combination is measured through the same shape.
struct BandGeometry {
    std::vector<double> probeHz;
    double              narrowHalf{};
    double              wideHalf{};
};

/// wideHalf is 6 * BW, but capped at 0.75 * f0 (a band reaching below DC is not
/// a band) and at 0.40 * fs - f0 (below Nyquist with margin). narrowHalf is
/// 0.75 * BW, never more than half the wide band or the ratio stops
/// discriminating. The probe spacing is BW / 8, i.e. just finer than the
/// window's own resolution, so the grid neither undersamples nor wastes work.
[[nodiscard]] BandGeometry makeBandGeometry(double f0, double expectedBw, double sampleRate) {
    BandGeometry g;
    g.wideHalf   = std::min({6.0 * expectedBw, 0.75 * f0, 0.40 * sampleRate - f0});
    g.narrowHalf = std::min(0.75 * expectedBw, 0.5 * g.wideHalf);
    const double spacing = std::min(expectedBw / 8.0, g.wideHalf / 40.0);
    for (double f = f0 - g.wideHalf; f <= f0 + g.wideHalf + 1.0e-9; f += spacing) {
        g.probeHz.push_back(f);
    }
    return g;
}

/// White noise at a nominal -12 dBFS RMS, drawn BLOCK-WISE from ONE persistent
/// generator: re-seeding per block would make the excitation periodic and put a
/// comb into the very spectrum this case measures. Xorshift32::nextFloat() is
/// uniform on [-1, 1] (random.h:59-63), RMS 1/sqrt(3), hence the fixed scale -
/// the exact per-block RMS is irrelevant to a RATIO of two band sums.
void fillNoiseBlock(Krate::DSP::Xorshift32& rng, std::vector<float>& block, float targetDbfs) {
    const float scale = Krate::DSP::dbToGain(targetDbfs) * 1.7320508f;  // sqrt(3)
    for (float& s : block) {
        s = rng.nextFloat() * scale;
    }
}

/// Drive `net` with `n` samples of fresh -12 dBFS stereo white noise, optionally
/// capturing the LEFT output channel. Rendered in 8192-sample blocks so the
/// 441 s worst case needs one 14 MB vector instead of four.
void renderNoise(ResonanceDriftNetwork& net, Krate::DSP::Xorshift32& rng, std::size_t n,
                 std::vector<float>* captureL) {
    constexpr std::size_t kBlock = 8192;
    std::vector<float>    inL(kBlock, 0.0f);
    std::vector<float>    inR(kBlock, 0.0f);
    std::vector<float>    outL(kBlock, 0.0f);
    std::vector<float>    outR(kBlock, 0.0f);
    if (captureL != nullptr) {
        captureL->clear();
        captureL->reserve(n);
    }
    for (std::size_t done = 0; done < n;) {
        const std::size_t chunk = std::min(kBlock, n - done);
        fillNoiseBlock(rng, inL, -12.0f);
        fillNoiseBlock(rng, inR, -12.0f);
        net.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), chunk);
        if (captureL != nullptr) {
            captureL->insert(captureL->end(), outL.begin(),
                             outL.begin() + static_cast<std::ptrdiff_t>(chunk));
        }
        done += chunk;
    }
}

} // namespace

TEST_CASE("ResonanceDriftNetwork_RealisedQ", "[resonance_drift_network]") {
    constexpr std::size_t kPeak     = 0;  // the one awake peak; 1..11 are dormant
    constexpr double      kLn1000D  = static_cast<double>(Krate::DSP::kLn1000);
    constexpr double      kPiD      = static_cast<double>(Krate::DSP::kPi);

    // Reported RT60 per [anchor][Q], for the "must CHANGE when peakQ changes"
    // arm after the loop.
    std::array<std::array<double, 3>, 3> reportedRt60{};
    std::array<double, 3>                anchorsSeen{};

    for (std::size_t caseIndex = 0; caseIndex < kRealisedQCases.size(); ++caseIndex) {
        const RealisedQCase& c = kRealisedQCases[caseIndex];
        CAPTURE(caseIndex, c.anchorHz, c.peakQ, c.sampleRate, c.frames);

        ResonanceDriftNetwork net;
        net.prepare(c.sampleRate, ResonanceDriftNetwork::PrepareConfig{});
        net.setSeed(0x5EED0017u + static_cast<std::uint32_t>(caseIndex));

        // Wander off: SC-017 measures a STATIC resonance, so every lane depth is
        // scaled to zero and the applied values sit on the anchor (FR-034).
        net.setWanderEnabled(false);
        // Fully wet - the dry path would add a flat pedestal to the spectrum and
        // bias every band ratio toward Q = 0.
        net.setMix(1.0f);
        // The FR-045 trim is a static scalar and cannot change a ratio of band
        // sums, but the FR-016 default of +30 dB would push the wide, low-Q
        // combinations toward kOutputClamp = 4.0, and a clamped sample is a
        // DISTORTED sample. 0 dB keeps the render linear; the clamp counter is
        // REQUIREd at zero below, so this is enforced rather than hoped for.
        net.setWetGain(0.0f);
        // Centre the peak so both channels carry the same wet signal: the
        // default pan POSITION is a seeded one-shot draw (FR-038), and a peak
        // drawn hard right would leave the analysed left channel near silent.
        net.setPeakPan(kPeak, 0.0f);
        net.setPeakPanWander(kPeak, 0.0f);
        for (std::size_t i = 1; i < ResonanceDriftNetwork::kMaxPeaks; ++i) {
            net.setPeakDormant(i, true);
        }

        // ---- the trap is armed HERE: Q first, THEN a frequency walk ----------
        net.setPeakQ(kPeak, c.peakQ);
        net.setPeakAnchorHz(kPeak, static_cast<float>(0.5 * c.anchorHz));

        const double rt60Seconds = static_cast<double>(c.peakQ) * kLn1000D / (kPiD * c.anchorHz);
        // 200 control steps clears the FR-035 slews (a 3.06-octave Q move at the
        // default 0.05 oct/step is 62 steps, the one-octave anchor walk is 50 at
        // 0.02 oct/step, and they run in sequence), which is also SC-014's
        // ">= 100 control chunks of settling" clause with margin.
        constexpr std::size_t kSlewSettleSamples =
            200 * ResonanceDriftNetwork::kControlChunkSamples;
        Krate::DSP::Xorshift32 rng{0xC0FFEE01u + static_cast<std::uint32_t>(caseIndex)};
        renderNoise(net, rng, kSlewSettleSamples, nullptr);

        // The second walk: an octave up, one setFrequency per control step, with
        // Q unchanged throughout. Mutation (ii) has nothing to bite on without
        // it. The extra 3 * RT60 lets the filter's transient decay 180 dB before
        // the measurement window opens.
        net.setPeakAnchorHz(kPeak, static_cast<float>(c.anchorHz));
        const auto ringSettleSamples =
            static_cast<std::size_t>(std::llround(3.0 * rt60Seconds * c.sampleRate));
        renderNoise(net, rng, kSlewSettleSamples + ringSettleSamples, nullptr);

        // ---- the FR-052 read surface, sampled AFTER the frequency writes -----
        const double reportedQ  = static_cast<double>(net.getPeakCurrentQ(kPeak));
        const double f0Hz       = static_cast<double>(net.getPeakCurrentFrequency(kPeak));
        const double reportedRt = static_cast<double>(net.getPeakEquivalentRt60(kPeak));
        CAPTURE(reportedQ, f0Hz, reportedRt);
        REQUIRE(Krate::DSP::detail::isFinite(reportedQ));  // double overload, db_utils.h:124
        REQUIRE(reportedQ > 0.0);
        REQUIRE(f0Hz > 0.0);
        // The settling landed: the applied values ARE the configured ones. This
        // is the echo SC-017 exists to distrust - pinned here only so the
        // comparison below is against the number the caller asked for.
        CHECK(reportedQ == Catch::Approx(static_cast<double>(c.peakQ)).epsilon(0.005));
        CHECK(f0Hz == Catch::Approx(c.anchorHz).epsilon(0.001));

        // ---- render the measurement window ----------------------------------
        const double      expectedBw = f0Hz / reportedQ;
        const std::size_t windowLen =
            static_cast<std::size_t>(std::llround(kWindowLengthFactor * c.sampleRate / expectedBw));
        const std::size_t hop = windowLen / 2;
        REQUIRE(windowLen >= 2u);
        REQUIRE(hop >= 1u);
        // Exactly `frames` Welch frames: start = 0, hop, ..., hop * (frames - 1).
        const std::size_t measureSamples = windowLen + hop * (c.frames - 1);
        CAPTURE(expectedBw, windowLen, hop, measureSamples);

        std::vector<float> render;
        renderNoise(net, rng, measureSamples, &render);
        REQUIRE(render.size() == measureSamples);
        REQUIRE(allFinite(render));

        const float renderRms = rmsOf(render);
        CAPTURE(renderRms);
        REQUIRE(renderRms > 1.0e-7f);
        // A clamped sample is a distorted sample and would broaden the measured
        // resonance. FR-018's counter says whether that happened at all.
        REQUIRE(net.getClampEngagementCount() == 0u);

        // ---- the band-ratio fit ---------------------------------------------
        const BandGeometry g = makeBandGeometry(f0Hz, expectedBw, c.sampleRate);
        REQUIRE(g.probeHz.size() >= 20u);
        REQUIRE(g.narrowHalf > 0.0);
        REQUIRE(g.wideHalf > g.narrowHalf);

        const std::vector<double> power =
            welchPowerGrid(render, c.sampleRate, g.probeHz, windowLen, hop);
        const double wideSum = measuredBandSum(power, g.probeHz, f0Hz, g.wideHalf);
        REQUIRE(wideSum > 0.0);
        const double measuredRatio =
            measuredBandSum(power, g.probeHz, f0Hz, g.narrowHalf) / wideSum;
        const double fittedQ =
            fitQFromBandRatio(measuredRatio, g.probeHz, f0Hz, g.narrowHalf, g.wideHalf);
        const double fittedBandwidthHz = f0Hz / fittedQ;
        const double agreement         = fittedQ / reportedQ;
        const std::size_t probeCount   = g.probeHz.size();
        CAPTURE(g.narrowHalf, g.wideHalf, probeCount, measuredRatio, fittedQ, fittedBandwidthHz,
                agreement);

        // The fit sits inside its bracket rather than pinned to an end of it: a
        // pinned value is a fit that failed, not a measurement.
        CHECK(fittedQ > kFitQLow * 1.2);
        CHECK(fittedQ < kFitQHigh * 0.95);

        // ---- SC-017's 25 % ---------------------------------------------------
        // CHECK, not REQUIRE: a mutated build must report all nine readings in
        // one run (see the INJECTION ARM note above).
        CHECK(std::fabs(agreement - 1.0) <= 0.25);

        // ---- FR-032's reported RT60 ------------------------------------------
        // (a) the getter IS Q * ln1000 / (pi * f) over the network's own applied
        //     values - the identity resonance_drift_network.h:802-811 claims.
        CHECK(reportedRt == Catch::Approx(reportedQ * kLn1000D / (kPiD * f0Hz)).epsilon(0.001));
        // (b) and it agrees with the REALISED decay the fit implies, within the
        //     same tolerance. This is the arm a stale-decay build cannot
        //     satisfy: rt60ToQ(f, 1.0) pins the realised RT60 at 1.0 s for every
        //     peak, whatever Q the caller configured.
        const double measuredRt60 = fittedQ * kLn1000D / (kPiD * f0Hz);
        CAPTURE(measuredRt60);
        CHECK(std::fabs(measuredRt60 / reportedRt - 1.0) <= 0.25);

        reportedRt60[caseIndex / 3][caseIndex % 3] = reportedRt;
        anchorsSeen[caseIndex / 3]                 = c.anchorHz;
    }

    // -------------------------------------------------------------------------
    // "getPeakEquivalentRt60 MUST CHANGE when peakQ changes at a fixed anchor"
    // (SC-017). A build deriving Q from the stale decay table reports one RT60
    // per anchor whatever the caller configured, so the three readings collapse
    // onto each other and both assertions below go red.
    // -------------------------------------------------------------------------
    for (std::size_t anchorIndex = 0; anchorIndex < 3; ++anchorIndex) {
        const double anchorHz = anchorsSeen[anchorIndex];
        const double lowQ     = reportedRt60[anchorIndex][0];  // peakQ = 2
        const double midQ     = reportedRt60[anchorIndex][1];  // peakQ = 12
        const double highQ    = reportedRt60[anchorIndex][2];  // peakQ = 100
        CAPTURE(anchorHz, lowQ, midQ, highQ);
        REQUIRE(lowQ > 0.0);
        CHECK(midQ > lowQ);
        CHECK(highQ > midQ);
        // RT60 is linear in Q at a fixed frequency, so the span IS the Q span.
        CHECK(highQ / lowQ == Catch::Approx(50.0).epsilon(0.25));
    }
}

// =============================================================================
// T017 arm set - SC-006 seed determinism and SC-008 sample-rate independence
// =============================================================================
//
// Both cases here are STATISTICS, not goldens, and the two thresholds they
// carry are PROVISIONAL until tasks.md T019 measures their null distributions.
// No bit-exact float golden is stored anywhere: the only "== 0.0f" assertions
// below are same-binary, same-process, same-seed DIFFERENCES, which is the one
// form the no-bit-exact-goldens rule admits (and the form SC-006 (a) and (d)
// explicitly ask for).

namespace {

/// SC-006 (b)'s decorrelation bound on |r| between peak `i` of instance A and
/// peak `i` of instance B, two instances differing ONLY in seed.
///
/// **MEASURED, 2026-09-11**, across TWELVE seed pairs as the spec requires. The
/// worst |r| of the twelve peak pairs read mean 0.4213, sd 0.1038, min 0.2316,
/// max 0.6454; the mean |r| of the twelve read 0.1908 (sd 0.0404); the
/// same-seed anti-vacuity control read exactly 1.0000.
///
/// 0.75 STANDS, and the measurement is what justifies it rather than the
/// derivation below. It sits 0.105 (1.0 sd) above the observed maximum and
/// 0.25 below the same-seed control, which is the bracket this constant has to
/// live in: the calibration's mechanical "observed max + 3 sd" suggestion of
/// 0.9568 would clear the observed null with more room but leave only 0.043
/// against a build whose two seeds were not independent at all - i.e. it would
/// buy margin on the side that does not matter by spending it on the side that
/// does.
///
/// Where 0.75 came from before it was measured, kept because it agrees: the arm
/// records 20 * tau of an OU trajectory, i.e. roughly 20
/// effectively independent samples, so the sample correlation of two
/// INDEPENDENT such trajectories has a standard deviation near
/// 1 / sqrt(20) = 0.22. 0.75 is ~3.4 sigma out, which keeps a correct build
/// green across 12 pairs while still sitting far below the >= 0.95 a same-seed
/// pair reads. It is deliberately NOT Phase 2's 0.05: that figure is an
/// audio-domain statistic over ~480 000 effectively independent points of white
/// noise (spec.md:1126-1132 records why importing it here is wrong).
///
/// MEASURED BY: ResonanceDriftNetwork_MeasureThresholds, section
/// "SC-006 (b) kSeedDecorrelationR" (resonance_drift_network_perf_test.cpp,
/// [.calibration]). That section builds the distribution of the WORST of the
/// twelve peak pairs - the quantity this constant bounds - across twelve seed
/// pairs, and reports the mean beside it for context.
constexpr float kSeedDecorrelationR = 0.75f;

/// SC-006 (b)'s anti-vacuity control, in the SAME process and with the SAME
/// estimator: a same-seed pair must read at least this. Without it, an
/// estimator that returned 0 for everything - a mis-typed accumulator, an empty
/// record - would pass the decorrelation arm silently.
constexpr float kSameSeedFloorR = 0.95f;

/// SC-008 (c)'s rate-invariance tolerance on the 1/e decorrelation lag, as a
/// fraction of the 48 kHz reading.
///
/// **MEASURED, 2026-09-11, and RETAINED at the spec's +/- 15 % - by fixing the
/// ESTIMATOR rather than widening the bound.** The single-lane form this arm
/// used first had a 13.9 % relative spread of its own (1/e lag at one fixed
/// rate: mean 0.991 s, sd 0.138 over eight seeds), so cross-rate readings of
/// |ratio - 1| ran to 0.634 and the calibration's mechanical suggestion for
/// this constant was 1.25 - a "tolerance" the arm's own injection case (1.77)
/// barely clears and that could not separate a rate-dependent build from noise.
/// Pooling the twelve independent lanes' autocorrelation functions before
/// taking the 1/e crossing (decorrelationLagSecondsPooled) removes that spread
/// at no cost in fixture or runtime, and the spec's figure stands.
///
/// MEASURED BY: ResonanceDriftNetwork_MeasureThresholds, section
/// "SC-008 (c) the rate-invariance tolerance"
/// (resonance_drift_network_perf_test.cpp, [.calibration]). That section
/// reports TWO distributions: the estimator's own relative spread at ONE fixed
/// rate (the noise floor no tolerance can sit below) and the realised worst
/// cross-rate deviation this constant actually bounds.
constexpr double kRateInvarianceTolerance = 0.15;

/// Pearson correlation of two equal-length records, sample means removed.
///
/// Accumulated in double for the same reason autocorrelation() above is: the
/// records are log2 of a few hundred Hz (a value near 5.3) over tens of
/// thousands of steps, and a float sum of squares loses the mean-removed tail.
/// Returns 0 when either record is constant - a degenerate input, which the
/// caller guards against separately by asserting the same-seed control reads
/// ~1.0.
[[nodiscard]] float pearson(const std::vector<float>& a, const std::vector<float>& b) {
    REQUIRE(a.size() == b.size());
    REQUIRE(!a.empty());
    const auto n = static_cast<double>(a.size());

    double meanA = 0.0;
    double meanB = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        meanA += static_cast<double>(a[i]);
        meanB += static_cast<double>(b[i]);
    }
    meanA /= n;
    meanB /= n;

    double num = 0.0;
    double denA = 0.0;
    double denB = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const double da = static_cast<double>(a[i]) - meanA;
        const double db = static_cast<double>(b[i]) - meanB;
        num += da * db;
        denA += da * da;
        denB += db * db;
    }
    const double den = std::sqrt(denA * denB);
    return (den > 0.0) ? static_cast<float>(num / den) : 0.0f;
}

/// SC-008 (c)'s RATE-INVARIANT statistic: the smallest lag, IN SECONDS, at
/// which a trajectory's normalised autocorrelation falls to 1/e.
///
/// This is the quantity the criterion is about. The lag in STEPS is not
/// rate-invariant - there are fs / kControlChunkSamples control steps per
/// second, so it scales with fs by construction - while the lag in seconds is,
/// and a lane mapping that hardcoded a sample rate would move it.
///
/// Scanned on a 20 ms grid (stepsPerSecond / 50) and linearly interpolated
/// between the bracketing points, so the estimator's own resolution is ~2 % of
/// a 1 s lag, comfortably inside the tolerance it feeds. Lags are capped at a
/// third of the record so autocorrelation()'s own "n > 2 * lag" precondition
/// holds with room. Returns a negative value when the record never crosses,
/// which every caller REQUIREs against.
[[nodiscard]] double decorrelationLagSeconds(const std::vector<float>& x, double stepsPerSecond) {
    constexpr float kInvE = 0.36787944f;  // exp(-1)
    REQUIRE(stepsPerSecond > 0.0);

    const std::size_t maxLag = x.size() / 3;
    const auto coarse = std::max<std::size_t>(std::size_t{1},
                                              static_cast<std::size_t>(stepsPerSecond / 50.0));

    float previousR = 1.0f;  // r(0) is 1 by construction
    std::size_t previousLag = 0;
    for (std::size_t lag = coarse; lag < maxLag; lag += coarse) {
        const float r = autocorrelation(x, lag);
        if (r <= kInvE) {
            const float span = previousR - r;  // > 0: previousR was above kInvE
            const float frac = (span > 0.0f) ? ((previousR - kInvE) / span) : 0.0f;
            const double lagSteps = static_cast<double>(previousLag)
                                    + static_cast<double>(frac) * static_cast<double>(coarse);
            return lagSteps / stepsPerSecond;
        }
        previousR = r;
        previousLag = lag;
    }
    return -1.0;
}

/// The POOLED form of the estimator above, and the reason SC-008 (c) can keep
/// the spec's +/- 15 % tolerance instead of widening it eightfold.
///
/// MEASURED, 2026-09-11: the single-lane estimator's own null, over eight seeds
/// at ONE fixed rate, read a 1/e lag of mean 0.991 s with sd 0.138 - a 13.9 %
/// relative spread. Comparing two INDEPENDENT realisations of a 13.9 %-spread
/// estimator produces |ratio - 1| values of mean 0.191, max 0.634 across the
/// same eight seeds, so a +/- 15 % tolerance on the single-lane statistic
/// passes on seed luck and the calibration's own suggestion for it was +/- 125 %
/// - a tolerance that could not separate anything, since the arm's injection
/// case reads 1.77.
///
/// The estimator, not the bound, is what was wrong. The twelve lanes are twelve
/// INDEPENDENT draws of the same process (FR-005 salts), they all advance
/// whatever `numPeaks` says, and pooling them costs nothing: this averages the
/// twelve normalised autocorrelation functions at each lag and takes the 1/e
/// crossing of the AVERAGE, which is far steadier than averaging twelve
/// crossing points because a single lane's noisy early dip cannot move it.
template <std::size_t N>
[[nodiscard]] double decorrelationLagSecondsPooled(
    const std::array<std::vector<float>, N>& trajectories, double stepsPerSecond) {
    constexpr float kInvE = 0.36787944f;  // exp(-1)
    static_assert(N > 0u, "pooling needs at least one trajectory");
    REQUIRE(stepsPerSecond > 0.0);

    const std::size_t maxLag = trajectories[0].size() / 3;
    const auto coarse = std::max<std::size_t>(std::size_t{1},
                                              static_cast<std::size_t>(stepsPerSecond / 50.0));

    float previousR = 1.0f;  // the pooled r(0) is 1 by construction
    std::size_t previousLag = 0;
    for (std::size_t lag = coarse; lag < maxLag; lag += coarse) {
        double sum = 0.0;
        for (const std::vector<float>& t : trajectories) {
            sum += static_cast<double>(autocorrelation(t, lag));
        }
        const auto r = static_cast<float>(sum / static_cast<double>(N));
        if (r <= kInvE) {
            const float span = previousR - r;
            const float frac = (span > 0.0f) ? ((previousR - kInvE) / span) : 0.0f;
            const double lagSteps = static_cast<double>(previousLag)
                                    + static_cast<double>(frac) * static_cast<double>(coarse);
            return lagSteps / stepsPerSecond;
        }
        previousR = r;
        previousLag = lag;
    }
    return -1.0;
}

/// Sample-rate-parameterised twin of renderSilence() above. The T010 original is
/// pinned to kFs, and SC-008 (b) has to reach the same gate state at 44.1 and
/// 96 kHz before it can time the ramp.
void renderSilenceAt(ResonanceDriftNetwork& net, double fs, float ms) {
    const auto total = static_cast<std::size_t>(static_cast<double>(ms) * 0.001 * fs);
    std::array<float, kControlChunk> zeros{};
    std::array<float, kControlChunk> oL{};
    std::array<float, kControlChunk> oR{};
    for (std::size_t done = 0; done < total;) {
        const std::size_t chunk = std::min(kControlChunk, total - done);
        net.processBlock(zeros.data(), zeros.data(), oL.data(), oR.data(), chunk);
        done += chunk;
    }
}

/// Sample-rate-parameterised twin of msToReach() above - SC-002 (d)'s NAMED
/// gate estimator, which SC-008 (b) reuses verbatim at three rates. Entry `i` is
/// the gate AFTER sample `i + 1` was rendered, exactly as in the original.
[[nodiscard]] float msToReachAt(const std::vector<float>& trace, float threshold, double fs) {
    for (std::size_t i = 0; i < trace.size(); ++i) {
        if (trace[i] >= threshold) {
            return static_cast<float>(1000.0 * static_cast<double>(i + 1) / fs);
        }
    }
    return -1.0f;
}

/// Streaming RMS, so the 10 s arms below need no multi-megabyte capture buffers.
/// Double throughout, for the reason rmsOf() above states: these renders are
/// 4.8e5 samples and a float sum of squares loses the tail.
struct RmsAccumulator {
    double sumSq = 0.0;
    std::size_t count = 0;

    void add(const std::vector<float>& v) {
        for (const float s : v) {
            sumSq += static_cast<double>(s) * static_cast<double>(s);
        }
        count += v.size();
    }
    void addDifference(const std::vector<float>& a, const std::vector<float>& b) {
        for (std::size_t i = 0; i < a.size(); ++i) {
            const double d = static_cast<double>(a[i]) - static_cast<double>(b[i]);
            sumSq += d * d;
        }
        count += a.size();
    }
    [[nodiscard]] double rms() const {
        return (count == 0) ? 0.0 : std::sqrt(sumSq / static_cast<double>(count));
    }
};

} // namespace

TEST_CASE("ResonanceDriftNetwork_SeedDeterminism", "[resonance_drift_network]") {
    SECTION("(a) same seed, configuration and rate render identically over 10 s, both channels") {
        constexpr std::uint32_t kSeed = 0x5EED0001u;
        constexpr std::size_t kBlock = 512;
        const auto kTotal = static_cast<std::size_t>(10.0 * kFs);

        ResonanceDriftNetwork a;
        ResonanceDriftNetwork b;
        a.setSeed(kSeed);
        b.setSeed(kSeed);
        a.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
        b.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});

        Krate::DSP::Xorshift32 rng{0xD8175EEDu};
        std::vector<float> inL(kBlock, 0.0f);
        std::vector<float> inR(kBlock, 0.0f);
        std::vector<float> aL(kBlock, 0.0f);
        std::vector<float> aR(kBlock, 0.0f);
        std::vector<float> bL(kBlock, 0.0f);
        std::vector<float> bR(kBlock, 0.0f);

        float worstL = 0.0f;
        float worstR = 0.0f;
        float loudest = 0.0f;
        bool finite = true;
        for (std::size_t done = 0; done < kTotal; done += kBlock) {
            fillNoiseBlock(rng, inL, -12.0f);
            fillNoiseBlock(rng, inR, -12.0f);
            a.processBlock(inL.data(), inR.data(), aL.data(), aR.data(), kBlock);
            b.processBlock(inL.data(), inR.data(), bL.data(), bR.data(), kBlock);
            worstL = std::max(worstL, maxAbsDiff(aL, bL));
            worstR = std::max(worstR, maxAbsDiff(aR, bR));
            loudest = std::max(loudest, std::max(peakAbs(aL), peakAbs(aR)));
            finite = finite && allFinite(aL) && allFinite(aR);
        }

        CAPTURE(worstL, worstR, loudest);
        // Non-vacuity first: comparing two silences would satisfy the equality
        // below without proving anything about determinism.
        REQUIRE(loudest > 0.0f);
        REQUIRE(finite);
        REQUIRE(worstL == 0.0f);
        REQUIRE(worstR == 0.0f);
    }

    SECTION("(b) two seeds decorrelate over 20 tau, against a same-seed control") {
        // TRANSCRIBE INTO compliance.md: wanderRate = 1.0 Hz gives an OU
        // correlation time of tau = 1 / 1.0 = 1.0 s at lane decimation 1 (T008's
        // worked table), and the record is 20.0 s = 20 * tau. That is the cheap
        // option spec.md:1134-1137 permits, taken deliberately over the 600 s
        // the FR-016 default rate would need.
        //
        // Recorded at kMinUsableSampleRate for the same reason the T008 lane
        // arms are: the statistic is defined in SECONDS, FR-037's decimation is
        // sample-rate independent (the lane advance takes a FIXED
        // kControlChunkSamples argument), and 8000 / 64 = 125 control steps per
        // second exactly.
        constexpr float kWanderRateHz = 1.0f;
        constexpr std::size_t kTauSteps = kSlowStepsPerSecond;  // tau = 1 s
        constexpr std::size_t kSteps = 20 * kTauSteps;          // 20 * tau

        const auto record = [](std::uint32_t seed,
                               std::array<std::vector<float>, kNumPeaks>& out) {
            ResonanceDriftNetwork net;
            net.setSeed(seed);
            net.prepare(kSlowFs, ResonanceDriftNetwork::PrepareConfig{});
            net.setWanderRate(kWanderRateHz);
            REQUIRE(net.getLaneDecimation() == std::size_t{1});  // the tau above assumes it
            recordFreqLog2(net, kSteps, out);
        };

        std::array<std::vector<float>, kNumPeaks> trajA;
        std::array<std::vector<float>, kNumPeaks> trajB;
        std::array<std::vector<float>, kNumPeaks> trajAtwin;
        record(0x0B0B0001u, trajA);
        record(0x51DE0002u, trajB);
        record(0x0B0B0001u, trajAtwin);  // same seed as A: the anti-vacuity control

        // PAIRING, per spec.md:1138-1139: peak i of A against peak i of B.
        // TWELVE pairs - never all 24 trajectories cross-paired, which would mix
        // in pairs whose anchors differ by four octaves and report a
        // decorrelation that owes nothing to the seed.
        double sumAbsR = 0.0;
        float worstAbsR = 0.0f;
        for (std::size_t i = 0; i < kNumPeaks; ++i) {
            REQUIRE(trajA[i].size() == kSteps);
            const float crossSeed = std::fabs(pearson(trajA[i], trajB[i]));
            const float sameSeed = pearson(trajA[i], trajAtwin[i]);
            CAPTURE(i, crossSeed, sameSeed);
            REQUIRE(crossSeed <= kSeedDecorrelationR);
            REQUIRE(sameSeed >= kSameSeedFloorR);
            sumAbsR += static_cast<double>(crossSeed);
            worstAbsR = std::max(worstAbsR, crossSeed);
        }
        // Reported for compliance.md's null-distribution row (T019).
        const double meanAbsR = sumAbsR / static_cast<double>(kNumPeaks);
        CAPTURE(meanAbsR, worstAbsR, kSeedDecorrelationR);
        REQUIRE(worstAbsR <= kSeedDecorrelationR);
    }

    SECTION("(c) the audio of two differently seeded instances differs measurably") {
        constexpr std::size_t kBlock = 512;
        const auto kTotal = static_cast<std::size_t>(10.0 * kFs);

        ResonanceDriftNetwork a;
        ResonanceDriftNetwork b;
        a.setSeed(0x0B0B0001u);
        b.setSeed(0x51DE0002u);
        a.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
        b.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});

        Krate::DSP::Xorshift32 rng{0xC0FFEE11u};
        std::vector<float> inL(kBlock, 0.0f);
        std::vector<float> inR(kBlock, 0.0f);
        std::vector<float> aL(kBlock, 0.0f);
        std::vector<float> aR(kBlock, 0.0f);
        std::vector<float> bL(kBlock, 0.0f);
        std::vector<float> bR(kBlock, 0.0f);

        RmsAccumulator rmsA;
        RmsAccumulator rmsB;
        RmsAccumulator rmsDiff;
        for (std::size_t done = 0; done < kTotal; done += kBlock) {
            fillNoiseBlock(rng, inL, -12.0f);
            fillNoiseBlock(rng, inR, -12.0f);
            a.processBlock(inL.data(), inR.data(), aL.data(), aR.data(), kBlock);
            b.processBlock(inL.data(), inR.data(), bL.data(), bR.data(), kBlock);
            rmsA.add(aL);
            rmsA.add(aR);
            rmsB.add(bL);
            rmsB.add(bR);
            rmsDiff.addDifference(aL, bL);
            rmsDiff.addDifference(aR, bR);
        }

        REQUIRE(rmsA.rms() > 0.0);
        REQUIRE(rmsB.rms() > 0.0);
        // "relative to EITHER render" (spec.md:1145-1147), so the quieter of the
        // two is the denominator - the harder of the two readings.
        const double quieter = std::min(rmsA.rms(), rmsB.rms());
        const double relativeDb = 20.0 * std::log10(rmsDiff.rms() / quieter);
        CAPTURE(rmsA.rms(), rmsB.rms(), rmsDiff.rms(), relativeDb);
        REQUIRE(relativeDb >= -30.0);
    }

    SECTION("(d) reset() with no setter since prepare reproduces the post-prepare stream") {
        ResonanceDriftNetwork net;
        net.setSeed(0x2E5E7001u);
        net.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});

        // 2 s is 1500 control steps and 96 000 samples of ramp movement at
        // 48 kHz - long enough that any piece of state reset() failed to rewind
        // has had time to diverge audibly.
        const auto kTotal = static_cast<std::size_t>(2.0 * kFs);
        Krate::DSP::Xorshift32 rng{0x1234ABCDu};
        std::vector<float> inL(kTotal, 0.0f);
        std::vector<float> inR(kTotal, 0.0f);
        fillNoiseBlock(rng, inL, -12.0f);
        fillNoiseBlock(rng, inR, -12.0f);

        std::vector<float> firstL;
        std::vector<float> firstR;
        renderStereoBuffer(net, inL, inR, firstL, firstR);

        // The ONLY call between the two renders. Anything else here - a setter,
        // a re-seed - would make the comparison a statement about that call
        // instead of about reset().
        net.reset();

        std::vector<float> secondL;
        std::vector<float> secondR;
        renderStereoBuffer(net, inL, inR, secondL, secondR);

        const float loudest = std::max(peakAbs(firstL), peakAbs(firstR));
        const float worstL = maxAbsDiff(firstL, secondL);
        const float worstR = maxAbsDiff(firstR, secondR);
        CAPTURE(loudest, worstL, worstR);
        REQUIRE(loudest > 0.0f);  // non-vacuity: two silences prove nothing
        REQUIRE(worstL == 0.0f);
        REQUIRE(worstR == 0.0f);
    }
}

TEST_CASE("ResonanceDriftNetwork_SampleRateIndependence", "[resonance_drift_network]") {
    constexpr std::array<double, 3> kRates{44100.0, 48000.0, 96000.0};

    SECTION("(a) realised anchor frequencies agree within 0.1 % across the three rates") {
        std::array<std::array<float, kNumPeaks>, 3> realised{};
        for (std::size_t r = 0; r < kRates.size(); ++r) {
            ResonanceDriftNetwork net;
            net.setSeed(0x5A3E0001u);
            net.prepare(kRates[r], ResonanceDriftNetwork::PrepareConfig{});
            net.setWanderEnabled(false);
            advanceControlSteps(net, kSettleChunks);
            for (std::size_t i = 0; i < kNumPeaks; ++i) {
                realised[r][i] = net.getPeakCurrentFrequency(i);
            }
        }

        // The criterion's own precondition, asserted rather than assumed: every
        // FR-016 default anchor must sit below 0.45 * 44 100 = 19 845 Hz, or the
        // lowest rate's Nyquist clamp would be doing the comparing.
        const float lowestNyquistLimit =
            Krate::DSP::kMaxResonatorFrequencyRatio * static_cast<float>(kRates[0]);
        for (std::size_t i = 0; i < kNumPeaks; ++i) {
            CAPTURE(i, realised[0][i], realised[1][i], realised[2][i], lowestNyquistLimit);
            REQUIRE(realised[0][i] < lowestNyquistLimit);
            REQUIRE(realised[1][i] == Catch::Approx(realised[0][i]).epsilon(0.001));
            REQUIRE(realised[2][i] == Catch::Approx(realised[0][i]).epsilon(0.001));
        }
    }

    SECTION("(b) the FR-041 gate ramp measures 50 ms at every rate") {
        constexpr std::size_t kPeak = 5;
        for (const double fs : kRates) {
            ResonanceDriftNetwork net;
            net.setSeed(0x5A3E0002u);
            net.prepare(fs, ResonanceDriftNetwork::PrepareConfig{});
            net.setWanderEnabled(false);

            // FR-042's precondition, the same one T010's (d) arm establishes:
            // >= 50 ms at wake == 0, so the gate has landed on EXACTLY 0.0f.
            net.setPeakWake(kPeak, 0.0f);
            renderSilenceAt(net, fs, 100.0f);
            REQUIRE(net.getPeakGate(kPeak) == 0.0f);

            net.setPeakWake(kPeak, 1.0f);
            const auto rampSamples = static_cast<std::size_t>(
                static_cast<double>(ResonanceDriftNetwork::kGainRampMs) * 0.001 * fs);
            const std::vector<float> trace =
                traceGate(net, kPeak, 4 * rampSamples, 0, [](std::size_t) {});

            // The per-sample step bound is rate-DERIVED, not the 48 kHz
            // constant: 1.05 / (kGainRampMs * 0.001 * fs) halves at 96 kHz, and
            // transcribing kGateStepBound here would silently pass a build that
            // stepped twice as fast as it should.
            const auto stepBound = static_cast<float>(
                1.05 / (static_cast<double>(ResonanceDriftNetwork::kGainRampMs) * 0.001 * fs));
            const float reachedMs = msToReachAt(trace, 1.0f, fs);
            const float rise = maxRise(trace, 0.0f);
            CAPTURE(fs, reachedMs, rise, stepBound);
            REQUIRE(reachedMs > 0.0f);
            REQUIRE(reachedMs == Catch::Approx(50.0f).margin(kRampToleranceMs));
            REQUIRE(isMonotoneNonDecreasing(trace, 0.0f));
            REQUIRE(rise <= stepBound);
        }
    }

    SECTION("(c) the 1/e decorrelation lag in seconds is rate-invariant") {
        // WHY A LAG IN SECONDS AND NOT THREE AUTOCORRELATION ESTIMATES
        // (spec.md:1160-1178). BrownianDrift::prepare recomputes
        // controlDtSeconds_ = kControlRateInterval / sampleRate_
        // (brownian_drift.h:121-128), so the same seed at three rates is the
        // same draw sequence consumed at three cadences - three REALISATIONS of
        // one process, not one signal sampled three ways. Comparing their raw
        // lag-T/8 estimates compares realisation noise. The 1/e lag in SECONDS
        // is a property of the process, so it survives that.
        //
        // wanderRate = 1.0 Hz gives tau = 1 s, and the render is 50 s = 50 * tau,
        // which is the binding ">= 50 * tau" clause of spec.md:1179. (tasks.md
        // T017's parenthetical "e.g. 20 s" is 20 * tau, not 50; the longer,
        // spec-conforming render is taken - it is also the one that shrinks the
        // estimator noise the +/- 15 % tolerance has to absorb.)
        constexpr float kWanderRateHz = 1.0f;
        constexpr double kRenderSeconds = 50.0;

        const auto measure = [](double fs, float rateHz) {
            ResonanceDriftNetwork net;
            net.setSeed(0x5A3E0003u);
            // numPeaks = 1: the statistic is peak 0's trajectory alone, and all
            // 48 lanes still advance for every peak regardless of the count
            // (T008), so this changes nothing the arm measures while dropping
            // the per-sample render cost of a 50 s pass by an order of
            // magnitude.
            const ResonanceDriftNetwork::PrepareConfig cfg{.maxBlockSamples = 2048,
                                                           .numPeaks = 1};
            net.prepare(fs, cfg);
            net.setWanderRate(rateHz);
            // The FR-035 slew ceiling is expressed PER CONTROL STEP, and there
            // are fs / 64 control steps per second - so the ceiling is the one
            // part of this path that is NOT rate-invariant. At the FR-016
            // default (0.02 oct/step) it sits far above the ~0.25 oct/s the lane
            // actually moves and never binds, but raising it here makes that a
            // fact of the test rather than an assumption, so the arm measures
            // the LANE and not the limiter.
            net.setSlewCeilings(1.0f, 1.0f);

            const double stepsPerSecond = fs / static_cast<double>(kControlChunk);
            const auto steps = static_cast<std::size_t>(kRenderSeconds * stepsPerSecond);
            std::array<std::vector<float>, kNumPeaks> trajectory;
            recordFreqLog2(net, steps, trajectory);
            // POOLED over all twelve lanes, not peak 0 alone - see
            // decorrelationLagSecondsPooled for the measured null that forced
            // it. Every lane advances whatever numPeaks says (T008), so this
            // costs one extra pass over data already recorded.
            return decorrelationLagSecondsPooled(trajectory, stepsPerSecond);
        };

        const double lag44k = measure(44100.0, kWanderRateHz);
        const double lag48k = measure(48000.0, kWanderRateHz);
        const double lag96k = measure(96000.0, kWanderRateHz);
        CAPTURE(lag44k, lag48k, lag96k, kRateInvarianceTolerance);
        REQUIRE(lag44k > 0.0);
        REQUIRE(lag48k > 0.0);
        REQUIRE(lag96k > 0.0);

        const double reference = lag48k;
        REQUIRE(std::fabs(lag44k / reference - 1.0) <= kRateInvarianceTolerance);
        REQUIRE(std::fabs(lag96k / reference - 1.0) <= kRateInvarianceTolerance);

        // ---- INJECTION CHECK (spec.md:1184-1187) ----------------------------
        // The defect this arm exists to catch is a hardcoded sample rate in the
        // lane mapping - passing 48 000 to BrownianDrift::prepare instead of
        // sampleRate_. At 96 kHz that build assumes a control interval twice as
        // long as the real one, so it computes a = exp(-2 * dt_real / tau): the
        // lane decorrelates in half the wall-clock time and the statistic above
        // reads about 0.5 * reference.
        //
        // That defect is a header edit, so it is injected here through the
        // equivalent OBSERVABLE instead of a mutated build: a real 96 kHz render
        // at wanderRate = 0.5 Hz, i.e. tau = 2 s, which moves the lane's time
        // constant by the SAME factor of two in the opposite direction. An
        // estimator that separates 2 s from 1 s separates 0.5 s from 1 s
        // equally, so this proves the arm is not vacuous. 50 s is 25 * tau here,
        // ample for a 2 s lag.
        const double lagInjected = measure(96000.0, 0.5f);
        CAPTURE(lagInjected);
        REQUIRE(lagInjected > 0.0);
        REQUIRE(std::fabs(lagInjected / reference - 1.0) > kRateInvarianceTolerance);
        // Not merely "differs": the estimator TRACKS tau, so a doubled time
        // constant has to show up as a roughly doubled lag.
        REQUIRE(lagInjected > 1.5 * reference);
    }

    SECTION("(d) an anchor above 0.45 fs is clamped silently at 44.1 kHz") {
        constexpr double kLowFs = 44100.0;
        constexpr std::size_t kPeak = 11;

        ResonanceDriftNetwork net;
        net.setSeed(0x5A3E0004u);
        net.prepare(kLowFs, ResonanceDriftNetwork::PrepareConfig{});
        net.setWanderEnabled(false);
        REQUIRE(net.getClampEngagementCount() == std::uint32_t{0});

        const float expectedMax =
            Krate::DSP::kMaxResonatorFrequencyRatio * static_cast<float>(kLowFs);  // 19 845 Hz
        net.setPeakAnchorHz(kPeak, 30000.0f);

        // (i) clamped, and REPORTED clamped by the configuration read surface.
        REQUIRE(net.getPeakAnchorHz(kPeak) == Catch::Approx(expectedMax).epsilon(1.0e-5));

        // (ii) and by the realised read surface, once the FR-035 limiter has
        // walked there. The move is log2(19845 / 1278) = 3.96 octaves and the
        // default ceiling is 0.02 oct/step, so it needs at least 198 control
        // steps; 500 is that bound with room, and deliberately not kSettleChunks
        // (200), which would be decided by a couple of steps of margin.
        advanceControlSteps(net, 500);
        REQUIRE(net.getPeakCurrentFrequency(kPeak) == Catch::Approx(expectedMax).epsilon(0.001));

        // (iii) no non-finite value anywhere in a real render at the clamped
        // anchor. The drive is -30 dBFS rather than the -12 the other arms use
        // precisely so FR-018's +/- 4.0 output clamp cannot engage against the
        // +30 dB default wet trim - arm (iv) below is about the counter staying
        // at zero, and a clipped render would make it a statement about
        // headroom instead.
        constexpr std::size_t kBlock = 512;
        Krate::DSP::Xorshift32 rng{0x5A3E5A3Eu};
        std::vector<float> inL(kBlock, 0.0f);
        std::vector<float> inR(kBlock, 0.0f);
        std::vector<float> outL(kBlock, 0.0f);
        std::vector<float> outR(kBlock, 0.0f);
        bool finite = true;
        float loudest = 0.0f;
        const auto kTotal = static_cast<std::size_t>(0.5 * kLowFs);
        for (std::size_t done = 0; done < kTotal; done += kBlock) {
            fillNoiseBlock(rng, inL, -30.0f);
            fillNoiseBlock(rng, inR, -30.0f);
            net.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), kBlock);
            finite = finite && allFinite(outL) && allFinite(outR);
            loudest = std::max(loudest, std::max(peakAbs(outL), peakAbs(outR)));
        }
        CAPTURE(loudest, expectedMax);
        REQUIRE(finite);
        REQUIRE(loudest > 0.0f);

        // (iv) FR-025: an anchor clamp is a legitimate configuration outcome at
        // a low sample rate, NOT a signal-path event, so it must leave the
        // FR-052 counter alone.
        REQUIRE(net.getClampEngagementCount() == std::uint32_t{0});
    }
}

// =============================================================================
// T019 arm set - SC-020: the FR-045 wet-gain trim
// =============================================================================
//
// WHAT THIS CRITERION IS FOR. FR-045 exists because the network is a bank of
// twelve narrow bandpasses: at unity wet trim the reference patch sits ~33 dB
// below its own drive (twelve constant-0 dB-peak resonators at Q = 12 on the
// FR-016 anchors have a summed noise-equivalent bandwidth of ~605 Hz against a
// 24 kHz white-noise bandwidth, plus FR-019's 1/sqrt(12), plus the -6 dB
// default peak level). Without a trim, `mix` would be a near-mute below
// mix ~ 0.95 rather than a blend - which is Clarifications Q2's finding and the
// reason kDefaultWetGainDb exists at all.
//
// kDefaultWetGainDb IS MEASURED, NOT AUTHORED. The measuring case is
// ResonanceDriftNetwork_MeasureThresholds (resonance_drift_network_perf_test.cpp,
// tagged [.calibration], tasks.md T019); it renders this exact patch on this
// exact drive at a 0 dB trim, reports the passive gap across eight seeds, and
// suggests the constant as that gap's negation. THIS case is the compliance
// row, not the measurement: it asserts that the shipped constant lands inside
// the window recorded in compliance.md.
//
// SPEC CORRECTION C-13 IS ALREADY IN FORCE HERE. An earlier draft of SC-020 (b)
// isolated the wet contribution by DIFFERENCING the mix = 0.5 render against
// the mix = 0 one - i.e. 0.5*wetTrimmed - 0.5*dry, which with the wet path
// MUTED degenerates to 0.5*dry, an RMS of exactly dry - 6.02 dB, landing inside
// the "within roughly 6 dB" pass window. The criterion reported a healthy blend
// for the very failure it names. Arm (b) below therefore compares a mix = 1
// render against a mix = 0 render DIRECTLY and never differences two renders.
// =============================================================================

namespace {

/// SC-020 (a)'s window on |wet RMS - drive RMS| at the shipped
/// kDefaultWetGainDb.
///
/// **PROVISIONAL.** tasks.md T019's [.calibration] case measures this
/// distribution across >= 8 seeds and the measured figure - together with the
/// kDefaultWetGainDb it is tied to - replaces it, recorded in compliance.md.
/// The figure carried into the build is FR-045's own "within a few dB of the
/// drive RMS" read as 6 dB, which is also SC-020 (b)'s bound: at the analytic
/// -33 dB passive gap and a +30 dB trim the patch lands ~3 dB under its drive,
/// so 6 dB is one estimator-spread's margin around the intended result rather
/// than a number chosen to pass.
constexpr float kWetVsDriveWindowDb = 6.0f;

/// SC-020 (b)'s bound, the spec's own figure: |RMS_wet - RMS_dry| <= 6 dB.
constexpr float kWetVsDryWindowDb = 6.0f;

/// SC-020 (c)'s tolerance on the realised trim delta. It is tight (0.5 dB)
/// because the quantity is not statistical: the trim is a scalar on the wet sum
/// (FR-044's wetScaleRamp), both renders see the same seed and the same drive
/// samples, and the resonator states do not depend on the trim at all - so the
/// two RMS figures differ by EXACTLY the dB difference of the two trims unless
/// something clips. Both renders assert a zero clamp count for that reason.
constexpr float kTrimDeltaToleranceDb = 0.5f;

/// The one network seed every arm of SC-020 uses. Fixed, so the three renders
/// an arm compares differ only in the setting under test.
constexpr std::uint32_t kWetGainSeed = 0x5C200001u;

/// The one drive seed. Every render below draws the SAME white-noise samples,
/// which is what lets (b) and (c) compare two renders without a statistical
/// allowance for the drive.
constexpr std::uint32_t kWetGainDriveSeed = 0xD817F00Du;

struct ReferenceRender {
    double outRmsDb = -300.0;    ///< the render's own RMS, both channels pooled
    double driveRmsDb = -300.0;  ///< the REALISED input RMS, both channels pooled
    std::uint32_t clampEngagements = 0;
    bool allFinite = true;
    float peak = 0.0f;  ///< non-vacuity: a silent render is finite too
};

/// Render the FR-016 reference patch - numPeaks = 12, every default untouched,
/// AnchorMode::Free - on SC-001's broadband drive (white noise at -12 dBFS on
/// BOTH channels) at the requested trim and mix, and report the output RMS
/// against the realised drive RMS.
///
/// The first second is discarded. FR-045's trim rides the same LinearRamp as
/// FR-019's normalisation (kGainRampMs, 50 ms) and setWetGain is necessarily
/// called after prepare, so an unsettled render carries a head that is louder
/// than the patch asks for; twelve Q = 12 resonators also need time to ring up.
[[nodiscard]] ReferenceRender renderReferencePatch(float wetGainDb, float mix)
{
    ResonanceDriftNetwork net;
    net.setSeed(kWetGainSeed);
    net.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
    net.setMix(mix);
    net.setWetGain(wetGainDb);

    Krate::DSP::Xorshift32 rng{kWetGainDriveSeed};
    constexpr std::size_t kBlock = 512;
    const auto kSettle = static_cast<std::size_t>(1.0 * kFs);
    const auto kMeasure = static_cast<std::size_t>(10.0 * kFs);

    std::vector<float> inL(kBlock, 0.0f);
    std::vector<float> inR(kBlock, 0.0f);
    std::vector<float> outL(kBlock, 0.0f);
    std::vector<float> outR(kBlock, 0.0f);

    ReferenceRender r;
    double driveSumSq = 0.0;
    double outSumSq = 0.0;
    std::size_t counted = 0;

    for (std::size_t done = 0; done < kSettle + kMeasure; done += kBlock) {
        fillNoiseBlock(rng, inL, -12.0f);
        fillNoiseBlock(rng, inR, -12.0f);
        net.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), kBlock);
        if (done < kSettle) {
            continue;
        }
        r.allFinite = r.allFinite && allFinite(outL) && allFinite(outR);
        r.peak = std::max(r.peak, std::max(peakAbs(outL), peakAbs(outR)));
        for (std::size_t i = 0; i < kBlock; ++i) {
            driveSumSq += static_cast<double>(inL[i]) * static_cast<double>(inL[i])
                          + static_cast<double>(inR[i]) * static_cast<double>(inR[i]);
            outSumSq += static_cast<double>(outL[i]) * static_cast<double>(outL[i])
                        + static_cast<double>(outR[i]) * static_cast<double>(outR[i]);
        }
        counted += 2u * kBlock;
    }

    const auto toDb = [](double sumSq, std::size_t n) {
        if (n == 0u) {
            return -300.0;
        }
        return 20.0 * std::log10(std::sqrt(sumSq / static_cast<double>(n)) + 1.0e-30);
    };
    r.outRmsDb = toDb(outSumSq, counted);
    r.driveRmsDb = toDb(driveSumSq, counted);
    r.clampEngagements = net.getClampEngagementCount();
    return r;
}

} // namespace

TEST_CASE("ResonanceDriftNetwork_WetGainTrim", "[resonance_drift_network]") {
    SECTION("(a) the shipped trim lands the wet render inside the recorded window") {
        const ReferenceRender wet =
            renderReferencePatch(ResonanceDriftNetwork::kDefaultWetGainDb, 1.0f);

        CAPTURE(wet.outRmsDb, wet.driveRmsDb, wet.peak, wet.clampEngagements);
        REQUIRE(wet.allFinite);
        // Non-vacuity: a muted network would satisfy nothing below honestly.
        REQUIRE(wet.peak > 0.0f);
        // Linearity precondition. A clipped render's RMS is the CLAMP's, and
        // this arm would then be reporting on FR-018 rather than on FR-045.
        REQUIRE(wet.clampEngagements == std::uint32_t{0});
        // The realised drive really is the -12 dBFS SC-001 specifies, so the
        // window below is a statement about the network and not about an
        // unmeasured drive.
        REQUIRE(wet.driveRmsDb == Catch::Approx(-12.0).margin(0.2));

        const double gapDb = wet.outRmsDb - wet.driveRmsDb;
        CAPTURE(gapDb, kWetVsDriveWindowDb);
        REQUIRE(std::fabs(gapDb) <= static_cast<double>(kWetVsDriveWindowDb));
    }

    SECTION("(b) the wet path is a real, blend-sized signal, isolated directly") {
        // CORRECTION C-13: mix = 1 against mix = 0, never a difference of two
        // renders. Both see the same seed and the same drive samples.
        const ReferenceRender wet =
            renderReferencePatch(ResonanceDriftNetwork::kDefaultWetGainDb, 1.0f);
        const ReferenceRender dry =
            renderReferencePatch(ResonanceDriftNetwork::kDefaultWetGainDb, 0.0f);
        const ReferenceRender half =
            renderReferencePatch(ResonanceDriftNetwork::kDefaultWetGainDb, 0.5f);

        REQUIRE(wet.allFinite);
        REQUIRE(dry.allFinite);
        REQUIRE(half.allFinite);
        REQUIRE(wet.clampEngagements == std::uint32_t{0});
        REQUIRE(dry.clampEngagements == std::uint32_t{0});
        REQUIRE(half.clampEngagements == std::uint32_t{0});

        // The mix = 0 render is FR-043's passthrough, so its RMS IS the drive's;
        // asserting it here means the comparison below cannot be satisfied by a
        // dry path that is itself broken.
        CAPTURE(dry.outRmsDb, dry.driveRmsDb);
        REQUIRE(dry.outRmsDb == Catch::Approx(dry.driveRmsDb).margin(0.05));

        const double wetVsDryDb = wet.outRmsDb - dry.outRmsDb;
        CAPTURE(wet.outRmsDb, wetVsDryDb, kWetVsDryWindowDb);
        REQUIRE(std::fabs(wetVsDryDb) <= static_cast<double>(kWetVsDryWindowDb));

        // ---- monotonicity, and the ONE place this arm departs from the
        //      spec's literal wording (BUILD-STAGE NOTE, T019) ---------------
        // SC-020 (b) keeps the mix = 0.5 render "only as a monotonicity check,
        // its RMS lying between the two". Taken literally - strictly between -
        // that is unsatisfiable by any correct implementation, for the same
        // class of reason C-13 caught in the original (b): FR-044's crossfade
        // is out = (1-m)*dry + m*wetScaled, so at m = 0.5 the render is
        // 0.5*(dry + wetScaled) and its RMS is
        // 0.5*sqrt(A^2 + B^2 + 2*rho*A*B). With A ~ B (which is exactly what
        // (b) has just asserted) and the two signals not fully correlated, that
        // sits BELOW both endpoints - at rho = 0 and A = B it is 3.01 dB below
        // both. So the bracket is the one the arithmetic actually supports:
        //   * the UPPER side is exact and needs no allowance -
        //     0.5*(A + B) <= max(A, B) by the triangle inequality, for every
        //     rho <= 1, so a mix = 0.5 render can never be louder than the
        //     louder endpoint;
        //   * the LOWER side carries the uncorrelated-sum term, 3.01 dB, plus
        //     estimator margin.
        // A build that ignored `mix` entirely (half == wet) still fails the
        // upper side whenever wet is the louder endpoint, and a build that
        // muted the wet path (half == 0.5*dry) fails the lower side, so the
        // bracket still discriminates in both directions.
        constexpr double kUncorrelatedSumDb = 3.01;
        constexpr double kEstimatorMarginDb = 0.5;
        const double lowerDb =
            std::min(wet.outRmsDb, dry.outRmsDb) - kUncorrelatedSumDb - kEstimatorMarginDb;
        const double upperDb = std::max(wet.outRmsDb, dry.outRmsDb) + kEstimatorMarginDb;
        CAPTURE(half.outRmsDb, lowerDb, upperDb);
        REQUIRE(half.outRmsDb >= lowerDb);
        REQUIRE(half.outRmsDb <= upperDb);
    }

    SECTION("(c) setWetGain is a real, clamped control") {
        ResonanceDriftNetwork net;
        net.setSeed(kWetGainSeed);
        net.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});

        // The echo, clamped to [kMinWetGainDb, kMaxWetGainDb] = [-24, +48].
        net.setWetGain(0.0f);
        REQUIRE(net.getWetGain() == Catch::Approx(0.0f));
        net.setWetGain(12.5f);
        REQUIRE(net.getWetGain() == Catch::Approx(12.5f));
        net.setWetGain(999.0f);
        REQUIRE(net.getWetGain() == Catch::Approx(ResonanceDriftNetwork::kMaxWetGainDb));
        net.setWetGain(-999.0f);
        REQUIRE(net.getWetGain() == Catch::Approx(ResonanceDriftNetwork::kMinWetGainDb));

        // ...and it really moves the wet level, by exactly the dB difference.
        const ReferenceRender atDefault =
            renderReferencePatch(ResonanceDriftNetwork::kDefaultWetGainDb, 1.0f);
        const ReferenceRender atUnity = renderReferencePatch(0.0f, 1.0f);
        REQUIRE(atDefault.allFinite);
        REQUIRE(atUnity.allFinite);
        REQUIRE(atUnity.peak > 0.0f);
        REQUIRE(atDefault.clampEngagements == std::uint32_t{0});
        REQUIRE(atUnity.clampEngagements == std::uint32_t{0});

        const double measuredDeltaDb = atDefault.outRmsDb - atUnity.outRmsDb;
        const auto expectedDeltaDb =
            static_cast<double>(ResonanceDriftNetwork::kDefaultWetGainDb) - 0.0;
        CAPTURE(atDefault.outRmsDb, atUnity.outRmsDb, measuredDeltaDb, expectedDeltaDb);
        REQUIRE(measuredDeltaDb == Catch::Approx(expectedDeltaDb).margin(kTrimDeltaToleranceDb));
    }
}
