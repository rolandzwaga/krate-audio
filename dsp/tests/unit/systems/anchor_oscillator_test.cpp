// ==============================================================================
// Layer 3: System Tests - Profundum Anchor, interface TU
//                                    (Profundum Phase 3, specs/profundum-phase3-anchor)
// ==============================================================================
// Reference: specs/profundum-phase3-anchor/plan.md (S8.1)
//            specs/profundum-phase3-anchor/tasks.md (T006 wires this TU,
//                                                    T007, T008, T009, T011, T012, T013, T014, T019 fill it)
//            T013: mode envelope g (plan S4.6-S4.7; plan S1 C-3, C-8, C-12)
//            T014: FOLLOW (plan S4.10): SC-008(b)(c), SC-025 at the end of the TU; FOLLOW rows
//                  in SC-026(a) (arm 7), SC-006 (a)-(c) and SC-027(d)
//            T019: SC-007 matrix, SC-013, SC-014 matrix + FR-007 (in RealTimeSafe), SC-016 at the end of the TU
//
// SCOPE OF THIS TU (plan S8.1): SC-004, SC-005, SC-006, SC-007 (helper), SC-013,
//   SC-014, SC-016, SC-017, SC-026, SC-027; enum static_asserts;
//   MirrorTracksCore. Tags [systems][profundum][anchor].
// ==============================================================================

#include "anchor_oscillator_test_helpers.h"
#include "artifact_detection.h"   // ClickDetector (T012)
#include <render_fingerprint.h>   // T019 SC-016: the cross-rate smoke only, never a golden

#include <krate/dsp/core/scoped_denormal_mode.h>   // KRATE_HAS_SSE_DENORMAL_CONTROL, <xmmintrin.h>
#include <krate/dsp/processors/harmonic_fold.h>
#include <krate/dsp/processors/spectral_shape_recipe.h>
#include <krate/dsp/systems/anchor_oscillator.h>
#include <krate/dsp/systems/profundum_core.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

using Krate::DSP::AnchorOscillator;
using Krate::DSP::HarmonicFold;
using Krate::DSP::ProfundumCore;
using Krate::DSP::SpectralShapeRecipe;
namespace PT = Krate::DSP::ProfundumTest;

namespace {

using AW = AnchorOscillator::Waveform;
using AM = AnchorOscillator::Mode;
using ARP = AnchorOscillator::RetriggerPhase;

bool allEqual(const std::vector<float>& x, float v) {
    return std::all_of(x.begin(), x.end(), [v](float s) { return std::bit_cast<std::uint32_t>(s) == std::bit_cast<std::uint32_t>(v); });
}

}  // namespace

// =============================================================================
// FR-023, FR-024, FR-055, S1 C-2: calibration constants
// =============================================================================

TEST_CASE("AnchorOscillator_CalibrationConstants", "[systems][profundum][anchor]") {
    static_assert(AnchorOscillator::kMaxCoreH1Amplitude == HarmonicFold::kCalibrationPeak);
    static_assert(AnchorOscillator::kOutputCeiling <= 2.0f);
    static_assert(AnchorOscillator::kAnchorToH1 == 1.0f);    // plan S1 C-2
    static_assert(AnchorOscillator::kDefaultLevel == 0.5f);  // plan S1 C-2
    const double derived = std::sqrt(static_cast<double>(SpectralShapeRecipe::kPowerTarget))
                           * static_cast<double>(SpectralShapeRecipe::kCenterPanGain);
    REQUIRE(std::abs(derived - static_cast<double>(AnchorOscillator::kMaxCoreH1Amplitude)) <= 1e-7);
}

// =============================================================================
// FR-001, FR-003, FR-033, SC-014, SC-017 (D-6), S1 C-4: enums and traits
// =============================================================================

TEST_CASE("AnchorOscillator_EnumsAndTraits", "[systems][profundum][anchor]") {
    // FR-003, append-only.
    static_assert(static_cast<std::uint8_t>(AW::Sine) == 0);
    static_assert(static_cast<std::uint8_t>(AW::Triangle) == 1);
    static_assert(static_cast<std::uint8_t>(AW::RoundedSine) == 2);
    static_assert(static_cast<std::uint8_t>(AW::AsymmetricSine) == 3);
    static_assert(static_cast<std::uint8_t>(AW::OctaveDownSine) == 4);
    static_assert(static_cast<std::uint8_t>(AM::Lock) == 0);
    static_assert(static_cast<std::uint8_t>(AM::Follow) == 1);
    static_assert(static_cast<std::uint8_t>(AM::Punch) == 2);
    static_assert(static_cast<std::uint8_t>(AM::Ghost) == 3);

    // SC-017 enum mapping (D-6).
    static_assert(static_cast<std::uint8_t>(ARP::Reset)
                  == static_cast<std::uint8_t>(ProfundumCore::RetriggerPhase::Reset));
    static_assert(static_cast<std::uint8_t>(ARP::FreeRunning)
                  == static_cast<std::uint8_t>(ProfundumCore::RetriggerPhase::FreeRunning));

    // FR-001, FR-033.
    static_assert(std::is_copy_constructible_v<AnchorOscillator> && std::is_nothrow_move_constructible_v<AnchorOscillator>);
    static_assert(AnchorOscillator::latencySamples() == 0);

    // SC-014: every audio-thread method and observer is noexcept.
    using A = AnchorOscillator;
    static_assert(noexcept(std::declval<A&>().setControls(std::declval<const A::Controls&>())));
    static_assert(noexcept(std::declval<A&>().setFrequency(1.0f)));
    static_assert(noexcept(std::declval<A&>().setFollowRatio(1.0f)));
    static_assert(noexcept(std::declval<A&>().setCoreH1Amplitude(1.0f)));
    static_assert(noexcept(std::declval<A&>().setRetriggerPhase(ARP::Reset)));
    static_assert(noexcept(std::declval<A&>().noteOn(1.0f)));
    static_assert(noexcept(std::declval<A&>().noteOn(1.0f, false)));
    static_assert(noexcept(std::declval<A&>().processBlock(nullptr, nullptr, 0)));
    static_assert(noexcept(std::declval<A&>().processBlock(nullptr, nullptr, 0, nullptr, nullptr)));
    static_assert(noexcept(std::declval<const A&>().modeGain()));
    static_assert(noexcept(std::declval<const A&>().phase()));
    static_assert(noexcept(std::declval<const A&>().phaseIncrement()));
    static_assert(noexcept(std::declval<const A&>().isSounding()));
    static_assert(noexcept(std::declval<const A&>().pendingControls()));
    static_assert(noexcept(std::declval<const A&>().stateFinite()));
    static_assert(noexcept(std::declval<const A&>().mirrorState()));
    static_assert(noexcept(A::latencySamples()));

    // S1 C-4: modeGain() returns double.
    static_assert(std::is_same_v<decltype(std::declval<const A&>().modeGain()), double>);
    static_assert(std::is_same_v<decltype(std::declval<const A&>().phase()), double>);
    static_assert(std::is_same_v<decltype(std::declval<const A&>().phaseIncrement()), double>);
    SUCCEED("static_asserts compiled");
}

// =============================================================================
// FR-057: the test main runs with FTZ on (pattern profundum_core_test.cpp:830-832)
// =============================================================================

TEST_CASE("AnchorOscillator_FtzPrecondition", "[systems][profundum][anchor]") {
#if KRATE_HAS_SSE_DENORMAL_CONTROL
    REQUIRE(_MM_GET_FLUSH_ZERO_MODE() == _MM_FLUSH_ZERO_ON);
#else
    SUCCEED("no SSE denormal control on this target");
#endif
}

// =============================================================================
// FR-004, FR-007, C-7: lifecycle no-ops
// =============================================================================

TEST_CASE("AnchorOscillator_LifecycleNoOps", "[systems][profundum][anchor]") {
    constexpr std::size_t kBlock = 64;

    SECTION("(i) before prepare: zeros, every setter and noteOn is a no-op") {
        AnchorOscillator a;
        std::vector<float> L(kBlock, 1.0f);
        std::vector<float> R(kBlock, 1.0f);
        a.setControls(AnchorOscillator::Controls{.waveform = AW::Triangle,
                                                 .mode = AM::Punch,
                                                 .level = 1.0f,
                                                 .punchBoostDb = 12.0f,
                                                 .punchTimeMs = 100.0f,
                                                 .ghostTimeMs = 50.0f});
        a.setFrequency(220.0f);
        a.setFollowRatio(2.0f);
        a.setCoreH1Amplitude(0.1f);
        a.setRetriggerPhase(ARP::FreeRunning);
        a.noteOn(110.0f);
        a.processBlock(L.data(), R.data(), kBlock);
        REQUIRE(allEqual(L, 0.0f));
        REQUIRE(allEqual(R, 0.0f));
        REQUIRE(a.stateFinite());
        REQUIRE(a.phase() == 0.0);
        REQUIRE_FALSE(a.isSounding());

        // The pre-prepare setters left nothing behind: defaults pending, no noteOn queued.
        a.prepare(48000.0);
        const AnchorOscillator::Controls p = a.pendingControls();
        REQUIRE(p.waveform == AW::Sine);
        REQUIRE(p.mode == AM::Lock);
        REQUIRE(p.level == AnchorOscillator::kDefaultLevel);
        const PT::AnchorRender r = PT::renderAnchor(a, 512, kBlock);
        REQUIRE(allEqual(r.L, 0.0f));
        REQUIRE_FALSE(a.isSounding());
    }

    SECTION("(ii) after prepare, before any noteOn: exact silence (C-7)") {
        AnchorOscillator a;
        a.prepare(48000.0);
        a.setFrequency(110.0f);
        const PT::AnchorRender r = PT::renderAnchor(a, 512, kBlock);
        REQUIRE(allEqual(r.L, 0.0f));
        REQUIRE(allEqual(r.R, 0.0f));
        REQUIRE_FALSE(a.isSounding());
        REQUIRE(a.stateFinite());
    }

    SECTION("(iii) null buffers and zero length: no write, no state change") {
        AnchorOscillator a;
        a.prepare(48000.0);
        a.noteOn(PT::anchorMidiHz(48));
        const PT::AnchorRender warm = PT::renderAnchor(a, 100, 37);
        REQUIRE(std::any_of(warm.L.begin(), warm.L.end(), [](float x) { return x != 0.0f; }));
        const double phase0 = a.phase();
        const double g0 = a.modeGain();
        const double inc0 = a.phaseIncrement();

        std::vector<float> L(kBlock, 1.0f);
        std::vector<float> R(kBlock, 1.0f);
        a.processBlock(nullptr, R.data(), kBlock);
        REQUIRE(allEqual(R, 1.0f));
        a.processBlock(L.data(), nullptr, kBlock);
        REQUIRE(allEqual(L, 1.0f));
        a.processBlock(L.data(), R.data(), 0);
        REQUIRE(allEqual(L, 1.0f));
        REQUIRE(allEqual(R, 1.0f));
        REQUIRE(std::bit_cast<std::uint64_t>(a.phase()) == std::bit_cast<std::uint64_t>(phase0));
        REQUIRE(std::bit_cast<std::uint64_t>(a.modeGain()) == std::bit_cast<std::uint64_t>(g0));
        REQUIRE(std::bit_cast<std::uint64_t>(a.phaseIncrement()) == std::bit_cast<std::uint64_t>(inc0));
    }

    SECTION("(iv) prepare clamps the rate to 22050") {
        AnchorOscillator lo;
        AnchorOscillator ref;
        lo.prepare(10000.0);
        ref.prepare(22050.0);
        lo.noteOn(PT::anchorMidiHz(48));
        ref.noteOn(PT::anchorMidiHz(48));
        const PT::AnchorRender rl = PT::renderAnchor(lo, 2048, 1, nullptr, nullptr, true);
        const PT::AnchorRender rr = PT::renderAnchor(ref, 2048, 1, nullptr, nullptr, true);
        REQUIRE(rl.inc.size() == rr.inc.size());
        std::size_t mismatches = 0;
        for (std::size_t i = 0; i < rl.inc.size(); ++i) {
            if (std::bit_cast<std::uint64_t>(rl.inc[i]) != std::bit_cast<std::uint64_t>(rr.inc[i]))
                ++mismatches;
        }
        REQUIRE(mismatches == 0);
        REQUIRE(rl.inc.back() > 0.0);
        REQUIRE(PT::samplesBitEqual(rl.L.data(), rr.L.data(), rl.L.size()));
    }
}

// =============================================================================
// Plan-internal (S1 C-5): the S5 coefficient mirror tracks ProfundumCore bit for bit
// =============================================================================

TEST_CASE("AnchorOscillator_MirrorTracksCore", "[systems][profundum][anchor]") {
    const float fE1 = PT::anchorMidiHz(28);
    const float fB1 = PT::anchorMidiHz(35);   // E1 + 7 semitones
    const float fC3 = PT::anchorMidiHz(48);

    for (const double fs : {48000.0, 96000.0}) {
        INFO("fs " << fs);
        auto core = std::make_unique<ProfundumCore>();
        core->prepare(fs);
        core->setControls(PT::midGrid());
        auto anchor = std::make_unique<AnchorOscillator>();
        anchor->prepare(fs);

        const auto at = [fs](double seconds) { return static_cast<std::size_t>(std::llround(seconds * fs)); };
        const std::size_t total = at(2.0);
        const std::size_t n05 = at(0.5);
        const std::size_t n10 = at(1.0);
        const std::size_t n12 = at(1.2);
        const std::size_t n15 = at(1.5);

        // 0-0.5 s: 5 Hz ±1-semitone vibrato around E1. 1.0-1.2 s: +12-semitone raised-cosine bend
        // (up and back) on B1. Elsewhere no trajectory (held f0).
        std::vector<float> traj(total, 0.0f);
        for (std::size_t i = 0; i < n05; ++i) {
            const double t = static_cast<double>(i) / fs;
            traj[i] = static_cast<float>(static_cast<double>(fE1) * std::exp2(std::sin(PT::kTwoPi * 5.0 * t) / 12.0));
        }
        for (std::size_t i = n10; i < n12; ++i) {
            const double t = static_cast<double>(i - n10) / fs;
            const double semis = 12.0 * 0.5 * (1.0 - std::cos(PT::kTwoPi * t / 0.2));
            traj[i] = static_cast<float>(static_cast<double>(fB1) * std::exp2(semis / 12.0));
        }

        float cL = 0.0f;
        float cR = 0.0f;
        float aL = 0.0f;
        float aR = 0.0f;
        std::size_t checked = 0;
        std::size_t mismatches = 0;
        std::size_t firstMismatch = total;
        for (std::size_t i = 0; i < total; ++i) {
            if (i == 0) {
                core->setRetriggerPhase(ProfundumCore::RetriggerPhase::Reset);
                anchor->setRetriggerPhase(ARP::Reset);
                core->noteOn(fE1);
                anchor->noteOn(fE1);
            } else if (i == n05) {
                core->setRetriggerPhase(ProfundumCore::RetriggerPhase::FreeRunning);
                anchor->setRetriggerPhase(ARP::FreeRunning);
                core->noteOn(fB1);
                anchor->noteOn(fB1);
            } else if (i == n15) {
                core->setRetriggerPhase(ProfundumCore::RetriggerPhase::Reset);
                anchor->setRetriggerPhase(ARP::Reset);
                core->noteOn(fC3);
                anchor->noteOn(fC3);
            }
            const float* tp = (i < n05 || (i >= n10 && i < n12)) ? traj.data() + i : nullptr;
            core->processBlock(&cL, &cR, 1, tp);
            anchor->processBlock(&aL, &aR, 1, tp);

            const AnchorOscillator::MirrorState m = anchor->mirrorState();
            const bool same = std::bit_cast<std::uint32_t>(m.baseHz) == std::bit_cast<std::uint32_t>(core->baseFrequency())
                              && std::bit_cast<std::uint32_t>(m.currentHz) == std::bit_cast<std::uint32_t>(core->currentFrequency())
                              && std::bit_cast<std::uint32_t>(m.maskHz) == std::bit_cast<std::uint32_t>(core->maskFrequency());
            ++checked;
            if (!same) {
                if (mismatches == 0)
                    firstMismatch = i;
                ++mismatches;
            }
        }
        WARN("MirrorTracksCore fs " << fs << ": " << checked << " blocks checked, " << mismatches
                                    << " mismatches (first at " << firstMismatch << ")");
        REQUIRE(checked == total);
        REQUIRE(mismatches == 0);
        REQUIRE(anchor->stateFinite());
    }
}

// =============================================================================
// FR-010, FR-016, FR-035: sine / LOCK basics
// =============================================================================

TEST_CASE("AnchorOscillator_SineLockBasics", "[systems][profundum][anchor]") {
    constexpr double kFs = 48000.0;
    const float f0 = PT::anchorMidiHz(48);   // C3

    SECTION("first sample exactly 0 and the calibrated RMS") {
        AnchorOscillator a;
        a.prepare(kFs);
        a.setRetriggerPhase(ARP::Reset);
        a.noteOn(f0);
        const auto n = static_cast<std::size_t>(std::llround(2.5 * kFs));
        const PT::AnchorRender r = PT::renderAnchor(a, n, 64);
        REQUIRE(r.L[0] == 0.0f);
        REQUIRE(std::bit_cast<std::uint32_t>(r.L[0]) == 0u);   // +0.0, not -0.0

        const auto from = static_cast<std::size_t>(std::llround(1.0 * kFs));
        const double rmsDb = PT::rmsDbOverPeriods(r.L.data() + from, n - from, kFs, static_cast<double>(f0), 1.0);
        const double expectedDb = 20.0 * std::log10(static_cast<double>(AnchorOscillator::kAnchorToH1)
                                                    * static_cast<double>(AnchorOscillator::kDefaultLevel)
                                                    * static_cast<double>(AnchorOscillator::kMaxCoreH1Amplitude)
                                                    / std::sqrt(2.0));
        INFO("rms " << rmsDb << " dB, expected " << expectedDb << " dB");
        REQUIRE(std::abs(rmsDb - expectedDb) <= 0.1);
        REQUIRE(a.modeGain() == 1.0);
        REQUIRE(a.isSounding());
    }

    SECTION("phase() is the double sum of phaseIncrement() modulo 1") {
        AnchorOscillator a;
        a.prepare(kFs);
        a.setRetriggerPhase(ARP::Reset);
        a.noteOn(f0);
        const PT::AnchorRender r = PT::renderAnchor(a, 4800, 1, nullptr, nullptr, true);
        REQUIRE(r.phase.size() == 4800);
        REQUIRE(r.L[0] == 0.0f);
        double sum = 0.0;
        double worst = 0.0;
        for (std::size_t i = 0; i < r.phase.size(); ++i) {
            sum += r.inc[i];
            double d = r.phase[i] - (sum - std::floor(sum));
            d -= std::round(d);
            worst = std::max(worst, std::abs(d));
            REQUIRE(r.phase[i] >= 0.0);
            REQUIRE(r.phase[i] < 1.0);
        }
        INFO("worst |phase - sum mod 1| " << worst);
        REQUIRE(worst <= 1e-12);
        // δ is the locked C3 rate: within 1e-6 relative of f0/fs (the mirror is ε-exact, not 2πf/fs).
        REQUIRE(std::abs(r.inc.back() * kFs - static_cast<double>(f0)) <= 1e-6 * static_cast<double>(f0));
    }
}

// =============================================================================
// T019 helpers (SC-007 matrix, SC-013, SC-014 matrix, SC-016, FR-007)
// =============================================================================

namespace {

constexpr std::array<AW, 5> kWavesT019{AW::Sine, AW::Triangle, AW::RoundedSine, AW::AsymmetricSine,
                                       AW::OctaveDownSine};
constexpr std::array<AM, 4> kModesT019{AM::Lock, AM::Follow, AM::Punch, AM::Ghost};
constexpr std::array<ARP, 2> kPoliciesT019{ARP::Reset, ARP::FreeRunning};
constexpr std::array<std::size_t, 5> kHostBlocksT019{1, 37, 64, 512, 4096};

const char* waveNameT019(AW w) {
    switch (w) {
        case AW::Sine: return "sine";
        case AW::Triangle: return "triangle";
        case AW::RoundedSine: return "rounded";
        case AW::AsymmetricSine: return "asymmetric";
        case AW::OctaveDownSine: return "octave-down";
    }
    return "?";
}

const char* modeNameT019(AM m) {
    switch (m) {
        case AM::Lock: return "LOCK";
        case AM::Follow: return "FOLLOW";
        case AM::Punch: return "PUNCH";
        case AM::Ghost: return "GHOST";
    }
    return "?";
}

AW nextWaveT019(AW w) noexcept { return kWavesT019[(static_cast<std::size_t>(w) + 1) % kWavesT019.size()]; }

AM nextModeT019(AM m) noexcept { return kModesT019[(static_cast<std::size_t>(m) + 1) % kModesT019.size()]; }

AnchorOscillator::Controls controlsT019(AW w, AM m, float level) noexcept {
    return AnchorOscillator::Controls{
        .waveform = w, .mode = m, .level = level, .punchBoostDb = 9.0f, .punchTimeMs = 20.0f, .ghostTimeMs = 10.0f};
}

/// The T019 transition script for (w, m). Allocation-free itself (the SC-014 matrix runs it inside a
/// tracked window). `segment()` renders each of the three stretches:
///   1. a snap arm (Reset noteOn, C2) at level 0.8, r = 1;
///   2. a waveform switch to the next waveform (crossfade) with a level step to 0.3, a noteOn a fifth up
///      under `policy` with a re-arm (FreeRunning: the bridged PUNCH / GHOST re-arm), and r 1 -> 2 (the
///      FOLLOW glide);
///   3. a mode switch to the next mode (FR-044 bridge) with another waveform switch, a level step to 0.6,
///      an a1 step and a setFrequency.
template <class Segment>
void transitionScriptT019(AnchorOscillator& a, AW w, AM m, ARP policy, const Segment& segment) {
    a.setControls(controlsT019(w, m, 0.8f));
    a.setFollowRatio(1.0f);
    a.setCoreH1Amplitude(AnchorOscillator::kMaxCoreH1Amplitude);
    a.setRetriggerPhase(ARP::Reset);
    a.noteOn(PT::anchorMidiHz(36));
    segment();
    a.setControls(controlsT019(nextWaveT019(w), m, 0.3f));
    a.setRetriggerPhase(policy);
    a.noteOn(PT::anchorMidiHz(43), true);
    a.setFollowRatio(2.0f);
    segment();
    a.setControls(controlsT019(nextWaveT019(nextWaveT019(w)), nextModeT019(m), 0.6f));
    a.setCoreH1Amplitude(0.5f * AnchorOscillator::kMaxCoreH1Amplitude);
    a.setFrequency(PT::anchorMidiHz(41));
    segment();
}

bool sameFloatsT019(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0);
}

bool sameDoublesT019(const std::vector<double>& a, const std::vector<double>& b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(double)) == 0);
}

bool anyNonZeroT019(const std::vector<float>& x) {
    return std::any_of(x.begin(), x.end(), [](float s) { return s != 0.0f; });
}

/// 5 Hz ±1-semitone vibrato about f0 (an f0PerSample trajectory).
std::vector<float> vibratoT019(float f0, double fs, std::size_t n) {
    std::vector<float> v(n, f0);
    for (std::size_t i = 0; i < n; ++i) {
        const double semis = std::sin(PT::kTwoPi * 5.0 * static_cast<double>(i) / fs);
        v[i] = static_cast<float>(static_cast<double>(f0) * std::exp2(semis / 12.0));
    }
    return v;
}

}  // namespace

// =============================================================================
// SC-014, FR-053: no allocation on the audio thread (sine/LOCK arm; T019 extends it)
// =============================================================================
// Nothing but the Anchor runs inside a tracked window (no Catch2 macros); the liveness probe
// runs first so a mis-wired binary cannot pass vacuously (profundum_core_test.cpp pattern).
TEST_CASE("AnchorOscillator_RealTimeSafe", "[systems][profundum][anchor]") {
    auto& detector = TestHelpers::AllocationDetector::instance();

    detector.startTracking();
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    int* probe = new int[16];
    probe[0] = 42;
    volatile int* probeSink = probe;   // defeat new/delete elision
    const int probeObserved = probeSink[0];
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    delete[] probe;
    const std::size_t livenessCount = detector.stopTracking();
    REQUIRE(probeObserved == 42);
    REQUIRE(livenessCount >= std::size_t{1});

    constexpr double kFs = 48000.0;
    constexpr std::array<std::size_t, 5> kBlocks{1, 37, 64, 512, 4096};
    auto a = std::make_unique<AnchorOscillator>();
    a->prepare(kFs);   // control thread, outside the window

    std::vector<float> L(4096, 0.0f);
    std::vector<float> R(4096, 0.0f);
    std::vector<float> traj(4096, 0.0f);
    std::vector<float> ratio(4096, 1.0f);
    for (std::size_t i = 0; i < traj.size(); ++i)
        traj[i] = PT::anchorMidiHz(36) * static_cast<float>(1.0 + 0.0001 * static_cast<double>(i));
    const AnchorOscillator::Controls c{.waveform = AW::Sine,
                                       .mode = AM::Lock,
                                       .level = 0.7f,
                                       .punchBoostDb = 6.0f,
                                       .punchTimeMs = 40.0f,
                                       .ghostTimeMs = 15.0f};

    detector.startTracking();
    a->setControls(c);
    a->setFrequency(PT::anchorMidiHz(36));
    a->setFollowRatio(1.0f);
    a->setCoreH1Amplitude(0.2f);
    a->setRetriggerPhase(ARP::Reset);
    a->noteOn(PT::anchorMidiHz(36));
    for (const std::size_t b : kBlocks)
        a->processBlock(L.data(), R.data(), b);
    a->setRetriggerPhase(ARP::FreeRunning);
    a->noteOn(PT::anchorMidiHz(43), true);
    for (const std::size_t b : kBlocks)
        a->processBlock(L.data(), R.data(), b, traj.data(), ratio.data());
    a->noteOn(PT::anchorMidiHz(40), false);
    a->setFrequency(PT::anchorMidiHz(41));
    for (const std::size_t b : kBlocks)
        a->processBlock(L.data(), L.data(), b);   // aliased buffer
    const std::size_t count = detector.stopTracking();

    REQUIRE(count == 0);
    REQUIRE(a->stateFinite());
    REQUIRE(a->isSounding());

    // ---- T019 (SC-014 matrix): every waveform x mode with transitions active (crossfade, PUNCH / GHOST
    // bridged re-arm, r glide via setFollowRatio and via followRatioPerSample, level and a1 steps, mode
    // bridge), host blocks {1, 37, 64, 512, 4096}, two buffers and aliased ----
    std::vector<float> ratioGlide(4096, 1.0f);
    for (std::size_t i = 0; i < ratioGlide.size(); ++i)
        ratioGlide[i] = 1.0f + 0.5f * static_cast<float>(i) / 4096.0f;
    auto mx = std::make_unique<AnchorOscillator>();
    mx->prepare(kFs);   // control thread, outside the window
    const auto matrixSegment = [&]() {
        for (const std::size_t b : kBlocks)
            mx->processBlock(L.data(), R.data(), b, traj.data(), nullptr);
        for (const std::size_t b : kBlocks)
            mx->processBlock(L.data(), L.data(), b);   // aliased buffer
        mx->processBlock(L.data(), R.data(), 64, traj.data(), ratioGlide.data());
    };

    detector.startTracking();
    for (const AW w : kWavesT019) {
        for (const AM m : kModesT019)
            transitionScriptT019(*mx, w, m, ARP::FreeRunning, matrixSegment);
    }
    const std::size_t matrixCount = detector.stopTracking();

    std::printf("SC-014 matrix: %zu allocation(s) over 5 waveforms x 4 modes with transitions active\n", matrixCount);
    REQUIRE(matrixCount == 0);
    REQUIRE(mx->stateFinite());
    REQUIRE(mx->isSounding());

    // ---- FR-007 (i): processBlock(L, L, n) equals the L channel of the same sequence into two buffers,
    // every waveform and mode with transitions active ----
    {
        constexpr std::array<std::size_t, 4> kAliasBlocks{37, 512, 1, 64};
        std::size_t mismatches = 0;
        for (const AW w : kWavesT019) {
            for (const AM m : kModesT019) {
                AnchorOscillator x;
                AnchorOscillator y;
                x.prepare(kFs);
                y.prepare(kFs);
                std::vector<float> buf(512, 0.0f);
                std::vector<float> bl(512, 0.0f);
                std::vector<float> br(512, 0.0f);
                std::vector<float> outX;
                std::vector<float> outY;
                std::vector<float> outYR;
                transitionScriptT019(x, w, m, ARP::FreeRunning, [&]() {
                    for (const std::size_t b : kAliasBlocks) {
                        x.processBlock(buf.data(), buf.data(), b);
                        outX.insert(outX.end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(b));
                    }
                });
                transitionScriptT019(y, w, m, ARP::FreeRunning, [&]() {
                    for (const std::size_t b : kAliasBlocks) {
                        y.processBlock(bl.data(), br.data(), b);
                        outY.insert(outY.end(), bl.begin(), bl.begin() + static_cast<std::ptrdiff_t>(b));
                        outYR.insert(outYR.end(), br.begin(), br.begin() + static_cast<std::ptrdiff_t>(b));
                    }
                });
                const bool ok = sameFloatsT019(outX, outY) && sameFloatsT019(outY, outYR) && anyNonZeroT019(outY);
                if (!ok) {
                    ++mismatches;
                    std::printf("FR-007(i) %s / %s: aliased output differs from the two-buffer L channel\n",
                                waveNameT019(w), modeNameT019(m));
                }
            }
        }
        std::printf("FR-007(i): %zu of 20 waveform x mode cells differ (bar 0)\n", mismatches);
        REQUIRE(mismatches == 0);
    }

    // ---- FR-007 (ii): null-buffer and zero-length calls interleaved mid-PUNCH, mid-crossfade and
    // mid-glide change nothing: output, phase() and modeGain() traces bitwise equal to the sequence
    // without them, and the non-null buffer's sentinel untouched ----
    {
        constexpr std::array<std::size_t, 7> kNoOpBlocks{1, 3, 7, 16, 37, 64, 512};
        constexpr float kSentinel = 1234.5f;
        struct TraceT019 {
            std::vector<float> out;
            std::vector<double> phase, gain;
        };
        std::size_t traceMismatches = 0;
        std::size_t sentinelHits = 0;
        for (const AW w : kWavesT019) {
            for (const AM m : kModesT019) {
                AnchorOscillator x;
                AnchorOscillator y;
                x.prepare(kFs);
                y.prepare(kFs);
                std::vector<float> bl(512, 0.0f);
                std::vector<float> br(512, 0.0f);
                std::vector<float> sl(512, kSentinel);
                std::vector<float> sr(512, kSentinel);
                TraceT019 tx;
                TraceT019 ty;
                const auto record = [](const AnchorOscillator& inst, TraceT019& t, const std::vector<float>& l,
                                       std::size_t b) {
                    t.out.insert(t.out.end(), l.begin(), l.begin() + static_cast<std::ptrdiff_t>(b));
                    t.phase.push_back(inst.phase());
                    t.gain.push_back(inst.modeGain());
                };
                transitionScriptT019(x, w, m, ARP::FreeRunning, [&]() {
                    for (const std::size_t b : kNoOpBlocks) {
                        x.processBlock(bl.data(), br.data(), b);
                        record(x, tx, bl, b);
                        x.processBlock(nullptr, sr.data(), b);
                        x.processBlock(sl.data(), nullptr, b);
                        x.processBlock(sl.data(), sr.data(), 0);
                    }
                });
                transitionScriptT019(y, w, m, ARP::FreeRunning, [&]() {
                    for (const std::size_t b : kNoOpBlocks) {
                        y.processBlock(bl.data(), br.data(), b);
                        record(y, ty, bl, b);
                    }
                });
                // The trailing no-ops after the last real block left the observers untouched too.
                tx.phase.push_back(x.phase());
                tx.gain.push_back(x.modeGain());
                ty.phase.push_back(y.phase());
                ty.gain.push_back(y.modeGain());
                if (!allEqual(sl, kSentinel) || !allEqual(sr, kSentinel)) {
                    ++sentinelHits;
                    std::printf("FR-007(ii) %s / %s: a no-op call wrote into a sentinel buffer\n", waveNameT019(w),
                                modeNameT019(m));
                }
                const bool same = sameFloatsT019(tx.out, ty.out) && sameDoublesT019(tx.phase, ty.phase)
                                  && sameDoublesT019(tx.gain, ty.gain) && anyNonZeroT019(ty.out);
                if (!same) {
                    ++traceMismatches;
                    std::printf("FR-007(ii) %s / %s: output / phase() / modeGain() trace differs after no-op calls\n",
                                waveNameT019(w), modeNameT019(m));
                }
            }
        }
        std::printf("FR-007(ii): %zu trace mismatch(es), %zu sentinel write(s) over 20 cells (bar 0 / 0)\n",
                    traceMismatches, sentinelHits);
        REQUIRE(traceMismatches == 0);
        REQUIRE(sentinelHits == 0);
    }
}

// =============================================================================
// T008 (plan S8.2): the analytic test toolkit, exercised on synthetic data only
// =============================================================================

TEST_CASE("AnchorOscillator_AnalyticCurves", "[systems][profundum][anchor]") {
    // Raised cosine (D-4 closed form): g0 + a·½(1 − cos(π·n/len)).
    {
        constexpr double g0 = 0.25;
        constexpr double a = 2.98;
        constexpr std::size_t len = 240;
        REQUIRE(std::abs(PT::anchorRaisedCosine(g0, a, 0, len) - g0) <= 1e-15);
        REQUIRE(std::abs(PT::anchorRaisedCosine(g0, a, len, len) - (g0 + a)) <= 1e-15);
        REQUIRE(std::abs(PT::anchorRaisedCosine(g0, a, len / 2, len) - (g0 + a / 2.0)) <= 1e-15);
    }

    // Cubic Hermite with zero end slope (FR-041): values at the ends, slope gp0 at n = 0, 0 at n = len.
    {
        constexpr double g0 = 0.3;
        constexpr double gp0 = 2e-6;   // per sample: m0 = len·gp0 = 2
        constexpr double g1 = 1.0;
        constexpr std::size_t len = 1000000;
        const auto H = [&](std::size_t n) { return PT::anchorHermite(g0, gp0, g1, n, len); };
        REQUIRE(std::abs(H(0) - g0) <= 1e-15);
        REQUIRE(std::abs(H(len) - g1) <= 1e-15);

        // Central difference over n ± 1 at n = 0. The helper takes an unsigned n, so H(−1) is the
        // cubic's exact extension from H(0..3) (the fourth difference of a cubic is zero).
        const double hm1 = 4.0 * H(0) - 6.0 * H(1) + 4.0 * H(2) - H(3);
        const double slope0 = (H(1) - hm1) / 2.0;
        std::printf("AnalyticCurves: Hermite slope at 0 = %.12e (gp0 = %.12e)\n", slope0, gp0);
        REQUIRE(std::abs(slope0 - gp0) <= 1e-6 * std::abs(gp0));
        // n = len + 1 evaluates the cubic's extension (the helper does not clamp n).
        const double slopeEnd = (H(len + 1) - H(len - 1)) / 2.0;
        std::printf("AnalyticCurves: Hermite slope at len = %.12e\n", slopeEnd);
        REQUIRE(std::abs(slopeEnd) <= 1e-6 * std::abs(gp0));
    }

    // Peak second difference: max(|6Δ − 4m0|, |6Δ − 2m0|)/len².
    REQUIRE(PT::anchorHermitePeakD2(0.0, 0.0, 1.0, 100) == 6e-4);

    // It bounds the brute-force max |Δ²| of the sampled curve within 1 % on 20 seeded random sets.
    // (std::uniform_*_distribution output is library-specific; every assertion here is an inequality.)
    std::mt19937 rng(3);
    std::uniform_real_distribution<double> gDist(0.0, 2.0);
    std::uniform_real_distribution<double> mDist(-3.0, 3.0);
    std::uniform_int_distribution<int> lenDist(240, 2400);
    for (int set = 0; set < 20; ++set) {
        const double g0 = gDist(rng);
        const double g1 = gDist(rng);
        const auto len = static_cast<std::size_t>(lenDist(rng));
        const double gp0 = mDist(rng) / static_cast<double>(len);
        const double peak = PT::anchorHermitePeakD2(g0, gp0, g1, len);
        double brute = 0.0;
        for (std::size_t n = 1; n < len; ++n) {
            const double d2 = PT::anchorHermite(g0, gp0, g1, n + 1, len) - 2.0 * PT::anchorHermite(g0, gp0, g1, n, len)
                              + PT::anchorHermite(g0, gp0, g1, n - 1, len);
            brute = std::max(brute, std::abs(d2));
        }
        std::printf("AnalyticCurves: set %2d len %4zu peakD2 %.6e brute %.6e ratio %.6f\n", set, len, peak, brute,
                    brute / peak);
        REQUIRE(peak > 0.0);
        REQUIRE(brute <= peak * (1.0 + 1e-6) + 1e-14);   // + double rounding of Δ² on O(1) values
        REQUIRE(brute >= 0.99 * peak);
    }
}

TEST_CASE("AnchorOscillator_C1CheckerPositiveControl", "[systems][profundum][anchor]") {   // SC-006(a)
    using Kind = PT::AnchorSegmentSpec::Kind;
    constexpr double A = 2.98;
    constexpr std::size_t N = 240;
    constexpr std::size_t kPre = 100;
    constexpr std::size_t kPost = 100;
    constexpr double kPiSq = 9.869604401089358;
    const double cosineBound = A * kPiSq / (2.0 * static_cast<double>(N) * static_cast<double>(N));
    const std::size_t total = kPre + N + kPost + 1;

    // hold(g0) over [0, kPre] | raised cosine over [kPre, kPre + N] | hold(g0 + a) to the end
    const auto segsFor = [&](double a) {
        return std::array<PT::AnchorSegmentSpec, 3>{{
            {Kind::Hold, 0.0, 0.0, 0.0, 0.0, 0, kPre},
            {Kind::Cosine, 0.0, a, 0.0, a, kPre, N},
            {Kind::Hold, a, 0.0, 0.0, a, kPre + N, kPost},
        }};
    };
    const auto cosineCurve = [&](double a) {
        std::vector<double> g(total, 0.0);
        for (std::size_t i = 0; i < total; ++i) {
            if (i <= kPre)
                g[i] = 0.0;
            else if (i <= kPre + N)
                g[i] = PT::anchorRaisedCosine(0.0, a, i - kPre, N);
            else
                g[i] = a;
        }
        return g;
    };

    // (1) A sampled raised cosine passes.
    {
        const auto segs = segsFor(A);
        const auto g = cosineCurve(A);
        const PT::C1Report rep = PT::anchorCheckC1(g, segs, 0.0, 3.0);
        std::printf("C1Checker raised cosine: worstRatio %.6f worstBoundaryJump %.6e (bound %.6e) inRange %d pass %d\n",
                    rep.worstRatio, rep.worstBoundaryJump, cosineBound, rep.inRange ? 1 : 0, rep.pass ? 1 : 0);
        REQUIRE(rep.worstRatio <= 1.0);
        REQUIRE(rep.inRange);
        REQUIRE(rep.pass);
    }

    // (2) A linear ramp of the same height and length, then a hold: its corners break C¹.
    {
        const auto segs = segsFor(A);
        std::vector<double> g(total, 0.0);
        for (std::size_t i = 0; i < total; ++i) {
            if (i <= kPre)
                g[i] = 0.0;
            else if (i <= kPre + N)
                g[i] = A * static_cast<double>(i - kPre) / static_cast<double>(N);
            else
                g[i] = A;
        }
        const PT::C1Report rep = PT::anchorCheckC1(g, segs, 0.0, 3.0);
        std::printf("C1Checker linear ramp: worstRatio %.6f worstBoundaryJump %.6e (bound %.6e) inRange %d pass %d\n",
                    rep.worstRatio, rep.worstBoundaryJump, cosineBound, rep.inRange ? 1 : 0, rep.pass ? 1 : 0);
        REQUIRE(rep.worstBoundaryJump > cosineBound);
        REQUIRE_FALSE(rep.pass);
    }

    // (3) A raised cosine that leaves [lo, hi] = [0, 1] is out of range.
    {
        constexpr double A2 = 1.5;
        const auto segs = segsFor(A2);
        const auto g = cosineCurve(A2);
        const PT::C1Report rep = PT::anchorCheckC1(g, segs, 0.0, 1.0);
        std::printf("C1Checker out of range: worstRatio %.6f inRange %d pass %d\n", rep.worstRatio,
                    rep.inRange ? 1 : 0, rep.pass ? 1 : 0);
        REQUIRE_FALSE(rep.inRange);
        REQUIRE_FALSE(rep.pass);
    }
}

TEST_CASE("AnchorOscillator_ShsEstimatorControls", "[systems][profundum][anchor]") {   // SC-008(r) tool
    constexpr double fs = 48000.0;
    constexpr std::size_t kFrame = Krate::DSP::TestUtils::kLowFrequencyFftSize;   // 262144
    constexpr std::size_t kSettle = 262144;
    constexpr double kTwoPiD = 6.283185307179586;

    // Double-synthesised, float-stored: harmonics n = 1..40 at amplitude amp(n), analysed over the
    // 262144 samples that follow a 262144-sample settle (only the analysed frame is generated).
    const auto synth = [&](double f0, double (*amp)(int)) {
        std::vector<float> x(kFrame, 0.0f);
        for (std::size_t i = 0; i < kFrame; ++i) {
            const double t = static_cast<double>(i + kSettle) / fs;
            double s = 0.0;
            for (int n = 1; n <= 40; ++n) {
                const double an = amp(n);
                if (an != 0.0)
                    s += an * std::sin(kTwoPiD * static_cast<double>(n) * f0 * t);
            }
            x[i] = static_cast<float>(s);
        }
        return x;
    };
    const auto cents = [](double f, double ref) { return 1200.0 * std::log2(f / ref); };

    for (const double f0 : {20.6017, 41.2034, 82.4069, 130.8128}) {
        const auto full = synth(f0, [](int n) { return 1.0 / static_cast<double>(n); });
        const double fFull = PT::anchorShsF0(full.data(), fs, f0);
        std::printf("ShsEstimator f0 %.4f: full %.6f Hz (%+.6f cent)\n", f0, fFull, cents(fFull, f0));
        REQUIRE(std::abs(cents(fFull, f0)) <= 0.1);

        // Missing fundamental: h1 removed, still f0.
        const auto noH1 = synth(f0, [](int n) { return n == 1 ? 0.0 : 1.0 / static_cast<double>(n); });
        const double fNoH1 = PT::anchorShsF0(noH1.data(), fs, f0);
        std::printf("ShsEstimator f0 %.4f: no h1 %.6f Hz (%+.6f cent)\n", f0, fNoH1, cents(fNoH1, f0));
        REQUIRE(std::abs(cents(fNoH1, f0)) <= 1.0);

        // Only even harmonics of f0 (harmonic m of 2f0 at amplitude 1/m): returns 2f0, not the hint.
        const auto even = synth(f0, [](int n) { return (n % 2) == 0 ? 2.0 / static_cast<double>(n) : 0.0; });
        const double fEven = PT::anchorShsF0(even.data(), fs, f0);
        std::printf("ShsEstimator f0 %.4f: even only %.6f Hz (%+.6f cent re 2f0)\n", f0, fEven,
                    cents(fEven, 2.0 * f0));
        REQUIRE(std::abs(cents(fEven, 2.0 * f0)) <= 1.0);
    }
}

TEST_CASE("AnchorOscillator_StaticScannerControls", "[systems][profundum][anchor]") {   // SC-013
    const std::array<std::string_view, 6> positives{
        "static double x_;",
        "static float table_[16];",
        "static bool once_ = false;",
        "static double y_ = std::sqrt(2.0);",
        "void f() { static int n = 0; }",
        "static thread_local float t_;",
    };
    int flagged = 0;
    for (const std::string_view src : positives) {
        const std::vector<std::string> hits = PT::anchorFindMutableStatics(src);
        std::printf("StaticScanner positive \"%.*s\": %zu hit(s)\n", static_cast<int>(src.size()), src.data(),
                    hits.size());
        CHECK(hits.size() == 1);
        if (hits.size() == 1)
            ++flagged;
    }
    std::printf("StaticScanner: %d of %zu positive controls flagged\n", flagged, positives.size());
    REQUIRE(std::cmp_equal(flagged, positives.size()));

    const std::array<std::string_view, 8> negatives{
        "static constexpr float k = 1.0f;",
        "static const int m = 2;",
        "[[nodiscard]] static constexpr std::size_t latencySamples() noexcept { return 0; }",
        "static float bankEps(float b) noexcept;",
        "x = static_cast<int>(y);",
        "static_assert(true);",
        "// static int commented;",
        "const char* s = \"static int inString;\";",
    };
    std::string combined;
    for (const std::string_view src : negatives) {
        const std::vector<std::string> hits = PT::anchorFindMutableStatics(src);
        std::printf("StaticScanner negative \"%.*s\": %zu hit(s)\n", static_cast<int>(src.size()), src.data(),
                    hits.size());
        CHECK(hits.empty());
        combined.append(src);
        combined.push_back('\n');
    }
    REQUIRE(PT::anchorFindMutableStatics(combined).empty());
}

TEST_CASE("AnchorOscillator_SeparationRuleControls", "[systems][profundum][anchor]") {   // plan S1 C-14
    constexpr std::size_t kN = 15;

    // Sine-like: w1 = 1, everything else at float-noise level (below the −90 dB floor).
    std::vector<double> sineA(kN, 1e-13);
    sineA[0] = 1.0;
    std::vector<double> sineB(kN, 1e-14);
    sineB[0] = 1.0;

    // Triangle-like: odd harmonics at 1/n⁴ power (h3 = −19.08 dB), even entries at noise level.
    const auto triangle = [&](double noise) {
        std::vector<double> w(kN, noise);
        for (std::size_t i = 0; i < kN; i += 2) {
            const auto n = static_cast<double>(i + 1);
            w[i] = 1.0 / (n * n * n * n);
        }
        return w;
    };
    const auto triA = triangle(1e-13);
    const auto triB = triangle(1e-14);

    // Rounded-like: the triangle-like vector with h3 = 0.0625 (−12.04 dB), so h3 is the only difference.
    auto rounded = triA;
    rounded[2] = 0.0625;

    // Extra control: h3 5.9 dB above the triangle's is below the 6 dB rule.
    auto nearTri = triA;
    nearTri[2] = triA[2] * std::pow(10.0, 0.59);

    const bool sineSine = PT::anchorWaveformsSeparated(sineA, sineB);
    const bool triTri = PT::anchorWaveformsSeparated(triA, triB);
    const bool sineRounded = PT::anchorWaveformsSeparated(sineA, rounded);
    const bool triRounded = PT::anchorWaveformsSeparated(triA, rounded);
    const bool triNear = PT::anchorWaveformsSeparated(triA, nearTri);
    std::printf("SeparationRule: sine/sine %d tri/tri %d sine/rounded %d tri/rounded %d (7.04 dB at h3) "
                "tri/near %d (5.9 dB at h3)\n",
                sineSine ? 1 : 0, triTri ? 1 : 0, sineRounded ? 1 : 0, triRounded ? 1 : 0, triNear ? 1 : 0);
    REQUIRE_FALSE(sineSine);
    REQUIRE_FALSE(triTri);
    REQUIRE(sineRounded);
    REQUIRE(triRounded);
    REQUIRE_FALSE(triNear);
}

// =============================================================================
// T009: phase continuity, convention and reference (SC-026 (a)(b sine)(c)); FreeRunning
// bit-identity (SC-017 FreeRunning clause). tasks.md deviation 5: a FreeRunning noteOn's δ is in
// force from offset 0 of the noteOn block.
// =============================================================================

namespace {

constexpr double kRadToDegT009 = 57.295779513082320876798;

/// One event of a SC-026(a) scenario, applied before sample `at` is rendered.
struct ContinuityEvent {
    enum class Kind : std::uint8_t { NoteOn, SetFrequency };
    Kind kind;
    std::size_t at;
    ARP policy;
    float f0;
    bool rearm;
};

struct ContinuityScenario {
    const char* name;
    std::size_t total;
    std::vector<float> traj;     // f0PerSample values (whole render)
    std::vector<bool> useTraj;   // per sample: pass the trajectory pointer for this 1-sample block
    std::vector<ContinuityEvent> events;
};

/// Renders `sc` in 1-sample blocks (renderAnchor, perSample) and checks, at every sample n ≥ 1 that
/// is not a Reset noteOn, |wrap(phase[n] − phase[n−1]) − f_grid(n)/fs| ≤ 1e-9 cycles. f_grid is the
/// test's independent model: the sanitised f sampled at the last grid point (U = 16; the grid
/// restarts at a Reset noteOn), and from the noteOn sample itself for a FreeRunning noteOn
/// (deviation 5). At a Reset noteOn φ restarts from 0, so phase == phaseIncrement there.
void runContinuity(const ContinuityScenario& sc, double fs) {
    REQUIRE(sc.traj.size() == sc.total);
    REQUIRE(sc.useTraj.size() == sc.total);
    AnchorOscillator a;
    a.prepare(fs);

    std::vector<double> phase(sc.total, 0.0);
    std::vector<double> inc(sc.total, 0.0);
    std::vector<double> expected(sc.total, 0.0);
    std::vector<bool> resetAt(sc.total, false);
    std::vector<float> out(sc.total, 0.0f);

    float held = AnchorOscillator::kDefaultF0Hz;
    double fGrid = 0.0;
    std::size_t gridPhase = 0;
    std::size_t ev = 0;
    for (std::size_t i = 0; i < sc.total; ++i) {
        for (; ev < sc.events.size() && sc.events[ev].at == i; ++ev) {
            const ContinuityEvent& e = sc.events[ev];
            if (e.kind == ContinuityEvent::Kind::SetFrequency) {
                a.setFrequency(e.f0);
                held = e.f0;
                continue;
            }
            a.setRetriggerPhase(e.policy);
            a.noteOn(e.f0, e.rearm);
            held = e.f0;
            if (e.policy == ARP::Reset || i == 0) {
                gridPhase = 0;
                resetAt[i] = true;
            } else {
                fGrid = static_cast<double>(e.f0);   // deviation 5
            }
        }
        const float* tp = sc.useTraj[i] ? sc.traj.data() + i : nullptr;
        if (gridPhase % AnchorOscillator::kPitchUpdateInterval == 0)
            fGrid = static_cast<double>(tp != nullptr ? sc.traj[i] : held);
        if (tp != nullptr)
            held = sc.traj[i];   // end-of-block bookkeeping (profundum_core.h:285-286)
        const PT::AnchorRender r = PT::renderAnchor(a, 1, 1, tp, nullptr, true);
        out[i] = r.L[0];
        phase[i] = r.phase[0];
        inc[i] = r.inc[0];
        expected[i] = fGrid / fs;
        gridPhase = (gridPhase + 1) % AnchorOscillator::kControlInterval;
    }
    REQUIRE(ev == sc.events.size());

    double worst = 0.0;
    std::size_t worstAt = 0;
    std::size_t resets = 0;
    for (std::size_t n = 0; n < sc.total; ++n) {
        if (resetAt[n]) {
            ++resets;
            INFO(sc.name << ": Reset noteOn at sample " << n);
            REQUIRE(out[n] == 0.0f);   // FR-016: the first sample after a Reset noteOn
            REQUIRE(std::bit_cast<std::uint64_t>(phase[n]) == std::bit_cast<std::uint64_t>(inc[n]));   // φ restarted at 0
            continue;
        }
        if (n == 0)
            continue;
        double d = phase[n] - phase[n - 1];
        d -= std::floor(d + 0.5);   // [-0.5, 0.5)
        const double err = std::abs(d - expected[n]);
        if (err > worst) {
            worst = err;
            worstAt = n;
        }
    }
    std::printf("SC-026(a) %s: worst |dphi - f_grid/fs| %.3e cycles at sample %zu (bar 1e-9), %zu Reset noteOn(s)\n",
                sc.name, worst, worstAt, resets);
    INFO(sc.name << ": worst " << worst << " cycles at sample " << worstAt);
    REQUIRE(worst <= 1e-9);
    REQUIRE(a.stateFinite());
}

/// A raised-cosine Drop: −12 semitones and back over `len` samples, starting at `from`.
std::vector<float> dropTrajectory(float f0, std::size_t total, std::size_t from, std::size_t len) {
    std::vector<float> traj(total, f0);
    for (std::size_t i = from; i < std::min(total, from + len); ++i) {
        const double x = static_cast<double>(i - from) / static_cast<double>(len);
        const double semis = -12.0 * 0.5 * (1.0 - std::cos(PT::kTwoPi * x));
        traj[i] = static_cast<float>(static_cast<double>(f0) * std::exp2(semis / 12.0));
    }
    return traj;
}

double wrapDegT009(double d) {
    d = std::fmod(d, 360.0);
    if (d > 180.0)
        d -= 360.0;
    else if (d <= -180.0)
        d += 360.0;
    return d;
}

}  // namespace

TEST_CASE("AnchorOscillator_PhaseContinuity", "[systems][profundum][anchor]") {   // SC-026(a)
    constexpr double kFs = 48000.0;
    constexpr std::size_t kTotal = 48000;
    constexpr std::size_t kFreeRunAt = 24007;   // off the grid: 24007 mod 16 = 7
    const float fC2 = PT::anchorMidiHz(36);
    const auto atSec = [](double s) { return static_cast<std::size_t>(std::llround(s * kFs)); };
    const ContinuityEvent resetC2{ContinuityEvent::Kind::NoteOn, 0, ARP::Reset, fC2, true};
    const std::vector<float> flat(kTotal, fC2);
    const std::vector<bool> never(kTotal, false);
    const std::vector<bool> always(kTotal, true);

    SECTION("(1) FreeRunning noteOn +5 semitones, rearm = true") {
        const ContinuityScenario sc{
            "FreeRunning +5 rearm", kTotal, flat, never,
            {resetC2, ContinuityEvent{ContinuityEvent::Kind::NoteOn, kFreeRunAt, ARP::FreeRunning,
                                      PT::anchorMidiHz(41), true}}};
        runContinuity(sc, kFs);
    }

    SECTION("(2) FreeRunning noteOn +5 semitones, rearm = false") {
        const ContinuityScenario sc{
            "FreeRunning +5 no rearm", kTotal, flat, never,
            {resetC2, ContinuityEvent{ContinuityEvent::Kind::NoteOn, kFreeRunAt, ARP::FreeRunning,
                                      PT::anchorMidiHz(41), false}}};
        runContinuity(sc, kFs);
    }

    SECTION("(3) 200 ms glides to +7 and to -7 semitones") {
        std::vector<float> traj(kTotal, fC2);
        const std::size_t g1 = atSec(0.25);
        const std::size_t g2 = atSec(0.6);
        const std::size_t len = atSec(0.2);
        for (std::size_t i = 0; i < kTotal; ++i) {
            double semis = 0.0;
            if (i >= g1)
                semis = 7.0 * std::min(1.0, static_cast<double>(i - g1) / static_cast<double>(len));
            if (i >= g2)
                semis = 7.0 - 14.0 * std::min(1.0, static_cast<double>(i - g2) / static_cast<double>(len));
            traj[i] = static_cast<float>(static_cast<double>(fC2) * std::exp2(semis / 12.0));
        }
        const ContinuityScenario sc{"glide +-7", kTotal, traj, always, {resetC2}};
        runContinuity(sc, kFs);
    }

    SECTION("(4) Drop-shaped dip: -12 semitones and back over 200 ms") {
        const ContinuityScenario sc{"Drop dip", kTotal, dropTrajectory(fC2, kTotal, atSec(0.25), atSec(0.2)), always,
                                    {resetC2}};
        runContinuity(sc, kFs);
    }

    SECTION("(5) held setFrequency -> f0PerSample trajectory -> held") {
        constexpr std::size_t kToTraj = 12003;   // off the grid
        constexpr std::size_t kToHeld = 36011;   // off the grid
        std::vector<float> traj(kTotal, fC2);
        std::vector<bool> use(kTotal, false);
        for (std::size_t i = kToTraj; i < kToHeld; ++i) {
            const double t = static_cast<double>(i) / kFs;
            traj[i] = static_cast<float>(static_cast<double>(fC2)
                                         * std::exp2((3.0 + std::sin(PT::kTwoPi * 5.0 * t)) / 12.0));
            use[i] = true;
        }
        const ContinuityScenario sc{
            "held -> trajectory -> held", kTotal, traj, use,
            {resetC2, ContinuityEvent{ContinuityEvent::Kind::SetFrequency, kToHeld, ARP::Reset, PT::anchorMidiHz(38),
                                      true}}};
        runContinuity(sc, kFs);
    }

    SECTION("(6) waveform switch mid-cycle (T011)") {
        // Sine -> triangle at an off-grid, mid-cycle sample. The phase trace is bit-identical to the
        // unswitched render, and every increment is the static note's f/fs within 1e-9 cycles.
        constexpr std::size_t kSwitchAt = 24007;
        const auto render = [&](bool doSwitch) {
            AnchorOscillator a;
            a.prepare(kFs);
            a.setRetriggerPhase(ARP::Reset);
            a.noteOn(fC2);
            PT::AnchorRender r1 = PT::renderAnchor(a, kSwitchAt, 1, nullptr, nullptr, true);
            if (doSwitch) {
                AnchorOscillator::Controls c{};
                c.waveform = AW::Triangle;
                a.setControls(c);
            }
            const PT::AnchorRender r2 = PT::renderAnchor(a, kTotal - kSwitchAt, 1, nullptr, nullptr, true);
            r1.L.insert(r1.L.end(), r2.L.begin(), r2.L.end());
            r1.phase.insert(r1.phase.end(), r2.phase.begin(), r2.phase.end());
            REQUIRE(a.stateFinite());
            return r1;
        };
        const PT::AnchorRender sw = render(true);
        const PT::AnchorRender ref = render(false);
        REQUIRE(sw.phase.size() == kTotal);
        // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) - intentional bit-exact check
        REQUIRE(std::memcmp(sw.phase.data(), ref.phase.data(), kTotal * sizeof(double)) == 0);
        // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) - intentional bit-exact check
        REQUIRE_FALSE(std::memcmp(sw.L.data() + kSwitchAt, ref.L.data() + kSwitchAt,
                                  (kTotal - kSwitchAt) * sizeof(float))
                      == 0);   // the switch is audible
        double worst = 0.0;
        for (std::size_t n = 1; n < kTotal; ++n) {
            double d = sw.phase[n] - sw.phase[n - 1];
            d -= std::floor(d + 0.5);
            worst = std::max(worst, std::abs(d - static_cast<double>(fC2) / kFs));
        }
        std::printf("SC-026(a) waveform switch: worst |dphi - f/fs| %.3e cycles (bar 1e-9)\n", worst);
        REQUIRE(worst <= 1e-9);
    }

    SECTION("(7) LOCK <-> FOLLOW switches at r = 1.5 (T014)") {
        // Expected law (plan S4.10): r_eff·f_grid/fs, with the 16-sample linear δ glide starting at the
        // first grid sample after each switch (the switch latches at the block's offset 0, C-8): the
        // k-th sample of the glide advances by δ_from + (δ_to − δ_from)·(k + 1)/16.
        constexpr std::size_t kToFollow = 12003;   // off the grid
        constexpr std::size_t kToLock = 36011;     // off the grid
        constexpr float kR = 1.5f;
        AnchorOscillator::Controls lock{};
        lock.mode = AM::Lock;
        AnchorOscillator::Controls follow = lock;
        follow.mode = AM::Follow;
        AnchorOscillator a;
        a.prepare(kFs);
        a.setFollowRatio(kR);
        a.setControls(lock);
        a.setRetriggerPhase(ARP::Reset);
        a.noteOn(fC2);
        std::vector<double> phase;
        phase.reserve(kTotal);
        const auto renderTo = [&](std::size_t to) {
            const PT::AnchorRender r = PT::renderAnchor(a, to - phase.size(), 1, nullptr, nullptr, true);
            phase.insert(phase.end(), r.phase.begin(), r.phase.end());
        };
        renderTo(kToFollow);
        a.setControls(follow);
        renderTo(kToLock);
        a.setControls(lock);
        renderTo(kTotal);
        REQUIRE(phase.size() == kTotal);
        REQUIRE(a.stateFinite());

        const double d1 = static_cast<double>(fC2) / kFs;
        double cur = d1;
        double from = d1;
        double to = d1;
        double rPrev = 1.0;
        std::size_t glideStart = 0;
        std::size_t glideEnd = 0;   // exclusive
        double worst = 0.0;
        std::size_t worstAt = 0;
        std::size_t glides = 0;
        for (std::size_t n = 0; n < kTotal; ++n) {
            if (n % AnchorOscillator::kPitchUpdateInterval == 0) {
                const double rEff = (n >= kToFollow && n < kToLock) ? static_cast<double>(kR) : 1.0;
                if (rEff != rPrev) {
                    from = cur;
                    to = rEff * d1;
                    glideStart = n;
                    glideEnd = n + AnchorOscillator::kFollowGlideSamples;
                    ++glides;
                } else {
                    cur = rEff * d1;
                }
                rPrev = rEff;
            }
            double expected = cur;
            if (n >= glideStart && n < glideEnd) {
                expected = from + (to - from) * static_cast<double>(n - glideStart + 1)
                                      / static_cast<double>(AnchorOscillator::kFollowGlideSamples);
                if (n + 1 == glideEnd)
                    cur = to;
            }
            if (n == 0)
                continue;   // the Reset noteOn: φ restarts at 0
            double d = phase[n] - phase[n - 1];
            d -= std::floor(d + 0.5);
            const double err = std::abs(d - expected);
            if (err > worst) {
                worst = err;
                worstAt = n;
            }
        }
        std::printf("SC-026(a) LOCK <-> FOLLOW r 1.5: worst |dphi - law| %.3e cycles at sample %zu (bar 1e-9), "
                    "%zu glides\n",
                    worst, worstAt, glides);
        REQUIRE(glides == 2);
        REQUIRE(worst <= 1e-9);
    }
}

TEST_CASE("AnchorOscillator_PhaseConvention", "[systems][profundum][anchor]") {   // SC-026(b), every waveform
    constexpr double kFs = 48000.0;
    constexpr double kHalfPi = 1.57079632679489661923;
    for (const int midi : {28, 48}) {   // E1, C3
        const float f0 = PT::anchorMidiHz(midi);
        const double hz = static_cast<double>(f0);
        auto core = std::make_unique<ProfundumCore>();
        core->prepare(kFs);
        const PT::Render cr = PT::renderCore(*core, PT::midGrid(), f0, 2.5, ProfundumCore::RetriggerPhase::Reset,
                                             false, 512);
        PT::requireLREqual(cr.L.data(), cr.R.data(), cr.L.size());
        const auto start = static_cast<std::size_t>(std::llround(1.0 * kFs));

        for (const AW w : {AW::Sine, AW::Triangle, AW::RoundedSine, AW::AsymmetricSine, AW::OctaveDownSine}) {
            AnchorOscillator a;
            a.prepare(kFs);
            AnchorOscillator::Controls c{};
            c.waveform = w;
            a.setControls(c);
            a.setRetriggerPhase(ARP::Reset);
            a.noteOn(f0);
            const PT::AnchorRender ar = PT::renderAnchor(a, cr.L.size(), 512);
            INFO("waveform " << static_cast<int>(w) << " midi " << midi);
            REQUIRE(ar.L[0] == 0.0f);

            double d = 0.0;
            if (w == AW::OctaveDownSine) {
                // 2·θ_a(f0/2) − θ1(f0) on a window of whole f0/2 periods (≥ 1 s). goertzelPhase returns the
                // cosine phase, so each phase is moved to the sine convention (+π/2) before doubling: a
                // locked sin(φ/2) then reads 0 (raw phases would read a constant −90°).
                const double half = 0.5 * hz;
                const auto L = static_cast<std::size_t>(std::llround(std::ceil(half) * kFs / half));
                REQUIRE(start + L <= ar.L.size());
                const double ta = PT::goertzelPhase(ar.L.data() + start, L, kFs, half) + kHalfPi;
                const double tc = PT::goertzelPhase(cr.L.data() + start, L, kFs, hz) + kHalfPi;
                d = wrapDegT009((2.0 * ta - tc) * kRadToDegT009);
                std::printf("SC-026(b) octave-down midi %d: 2*theta_a - theta_1 = %+.6f deg (bar 1)\n", midi, d);
            } else {
                const auto L = static_cast<std::size_t>(std::llround(std::ceil(hz) * kFs / hz));
                REQUIRE(start + L <= ar.L.size());
                const double ta = PT::goertzelPhase(ar.L.data() + start, L, kFs, hz);
                const double tc = PT::goertzelPhase(cr.L.data() + start, L, kFs, hz);
                d = wrapDegT009((ta - tc) * kRadToDegT009);
                std::printf("SC-026(b) waveform %d midi %d: Goertzel phase anchor - core h1 = %+.6f deg (bar 1)\n",
                            static_cast<int>(w), midi, d);
            }
            CHECK(std::abs(d) <= 1.0);
        }
    }
}

TEST_CASE("AnchorOscillator_PhaseReference", "[systems][profundum][anchor]") {   // SC-026(c)
    constexpr double kFs = 48000.0;
    const float fC2 = PT::anchorMidiHz(36);
    const auto total = static_cast<std::size_t>(std::llround(0.75 * kFs));
    const auto dropFrom = static_cast<std::size_t>(std::llround(0.25 * kFs));
    const auto dropLen = static_cast<std::size_t>(std::llround(0.2 * kFs));
    const std::vector<float> drop = dropTrajectory(fC2, total, dropFrom, dropLen);
    const std::vector<float> preDrop(total, fC2);

    // The core is fed the Drop (1-sample blocks, the same calls as the Anchor).
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(kFs);
    core->setControls(PT::midGrid());
    core->setRetriggerPhase(ProfundumCore::RetriggerPhase::Reset);
    core->noteOn(fC2);
    std::vector<float> cL(total, 0.0f);
    std::vector<float> cR(total, 0.0f);
    for (std::size_t i = 0; i < total; ++i)
        core->processBlock(cL.data() + i, cR.data() + i, 1, drop.data() + i);
    PT::requireLREqual(cL.data(), cR.data(), total);

    // The Anchor in LOCK is fed the pre-Drop (resolved, constant C2) trajectory.
    AnchorOscillator a;
    a.prepare(kFs);
    a.setRetriggerPhase(ARP::Reset);
    a.noteOn(fC2);
    const PT::AnchorRender ar = PT::renderAnchor(a, total, 1, preDrop.data(), nullptr, true);
    REQUIRE(ar.phase.size() == total);

    // φ(end of Drop) − φ(start), unwrapped over the samples [dropFrom, dropFrom + dropLen).
    double unwrapped = 0.0;
    double reference = 0.0;
    for (std::size_t n = dropFrom; n < dropFrom + dropLen; ++n) {
        double d = ar.phase[n] - ar.phase[n - 1];
        d -= std::floor(d + 0.5);
        unwrapped += d;
        reference += static_cast<double>(preDrop[n]) / kFs;   // f_grid: the constant pre-Drop f
    }
    const double err = std::abs(unwrapped - reference);
    std::printf("SC-026(c): phase over the Drop %.9f cycles, integral of the pre-Drop trajectory %.9f cycles, "
                "|diff| %.3e (bar 1e-6)\n",
                unwrapped, reference, err);
    REQUIRE(err <= 1e-6);
}

TEST_CASE("AnchorOscillator_FreeRunningSameNoteIsSeamless", "[systems][profundum][anchor]") {   // SC-017
    constexpr double kFs = 48000.0;
    constexpr std::size_t kTotal = 48000;
    const float fC2 = PT::anchorMidiHz(36);
    for (const std::size_t block : {std::size_t{37}, std::size_t{512}}) {
        const std::size_t split = (std::size_t{24000} + block - 1) / block * block;   // the noteOn's block boundary

        const auto render = [&](bool interrupt) {
            AnchorOscillator a;
            a.prepare(kFs);
            a.setRetriggerPhase(ARP::Reset);
            a.noteOn(fC2);
            const PT::AnchorRender r1 = PT::renderAnchor(a, split, block);
            a.setRetriggerPhase(ARP::FreeRunning);
            if (interrupt)
                a.noteOn(fC2, true);
            const PT::AnchorRender r2 = PT::renderAnchor(a, kTotal - split, block);
            std::vector<float> out(r1.L);
            out.insert(out.end(), r2.L.begin(), r2.L.end());
            return out;
        };
        const std::vector<float> uninterrupted = render(false);
        const std::vector<float> interrupted = render(true);
        REQUIRE(uninterrupted.size() == kTotal);
        REQUIRE(interrupted.size() == kTotal);
        INFO("block " << block << ", noteOn at sample " << split);
        // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) - intentional bit-exact check
        REQUIRE(std::memcmp(uninterrupted.data(), interrupted.data(), kTotal * sizeof(float)) == 0);
    }
}

// =============================================================================
// T011: click-free onset (SC-017 onset; FR-016, plan S1 C-6)
// =============================================================================

namespace {

constexpr std::array<AW, 5> kAllWavesT011{AW::Sine, AW::Triangle, AW::RoundedSine, AW::AsymmetricSine,
                                          AW::OctaveDownSine};

AnchorOscillator::Controls waveControlsT011(AW w) {
    AnchorOscillator::Controls c{};
    c.waveform = w;
    c.mode = AM::Lock;
    c.level = 1.0f;
    return c;
}

double maxAbsDelta(const std::vector<float>& x, std::size_t from, std::size_t to) {
    double m = 0.0;
    for (std::size_t i = std::max<std::size_t>(from, 1); i < to; ++i)
        m = std::max(m, std::abs(static_cast<double>(x[i]) - static_cast<double>(x[i - 1])));
    return m;
}

}  // namespace

TEST_CASE("AnchorOscillator_OnsetClickFree", "[systems][profundum][anchor]") {
    constexpr double kFs = 48000.0;
    const auto onsetLen = static_cast<std::size_t>(std::llround(0.010 * kFs));
    const auto steadyFrom = static_cast<std::size_t>(std::llround(1.0 * kFs));
    const auto steadyTo = static_cast<std::size_t>(std::llround(2.0 * kFs));
    for (const int midi : {16, 28, 48}) {   // E0, E1, C3
        const float f0 = PT::anchorMidiHz(midi);
        for (const AW w : kAllWavesT011) {
            AnchorOscillator a;
            a.prepare(kFs);
            a.setControls(waveControlsT011(w));
            a.setRetriggerPhase(ARP::Reset);
            a.noteOn(f0);
            const PT::AnchorRender r = PT::renderAnchor(a, steadyTo, 512);
            INFO("waveform " << static_cast<int>(w) << " midi " << midi);
            REQUIRE(r.L[0] == 0.0f);   // FR-016: every waveform, the asymmetric sine through its ramp
            const double onset = maxAbsDelta(r.L, 1, onsetLen + 1);
            const double steady = maxAbsDelta(r.L, steadyFrom, steadyTo);
            REQUIRE(steady > 0.0);
            std::printf("SC-017 onset waveform %d midi %d: max|dy| first 10 ms %.6e, steady %.6e, ratio %.4f "
                        "(bar 1.1, model 1.0000)\n",
                        static_cast<int>(w), midi, onset, steady, onset / steady);
            CHECK(onset <= 1.1 * steady);
        }
    }
}

// =============================================================================
// T011: waveform crossfade (FR-014, plan S4.8, S1 C-8 / E-7)
// =============================================================================

namespace {

struct XfadeEvent {
    enum class Kind : std::uint8_t { Wave, ResetNoteOn };
    std::size_t at;
    Kind kind;
    AW wave;
};

/// C2, 48 kHz, level 1, LOCK: a Reset noteOn with `first`, then `total` samples in 512-sample blocks,
/// split at each event (an event applies before its sample is rendered, i.e. at a block's offset 0).
std::vector<float> renderXfade(AW first, std::size_t total, const std::vector<XfadeEvent>& events) {
    constexpr double kFs = 48000.0;
    AnchorOscillator a;
    a.prepare(kFs);
    a.setControls(waveControlsT011(first));
    a.setRetriggerPhase(ARP::Reset);
    a.noteOn(PT::anchorMidiHz(36));
    std::vector<float> out;
    out.reserve(total);
    std::size_t pos = 0;
    const auto renderTo = [&](std::size_t to) {
        if (to > pos) {
            const PT::AnchorRender r = PT::renderAnchor(a, to - pos, 512);
            out.insert(out.end(), r.L.begin(), r.L.end());
            pos = to;
        }
    };
    for (const XfadeEvent& ev : events) {
        REQUIRE(ev.at >= pos);
        renderTo(ev.at);
        if (ev.kind == XfadeEvent::Kind::Wave) {
            a.setControls(waveControlsT011(ev.wave));
        } else {
            a.setRetriggerPhase(ARP::Reset);
            a.noteOn(PT::anchorMidiHz(36));
        }
    }
    renderTo(total);
    REQUIRE(out.size() == total);
    REQUIRE(a.stateFinite());
    return out;
}

double xfadeWeight(std::size_t n, std::size_t len) {
    return 0.5 * (1.0 - std::cos(PT::kAnchorPiD * static_cast<double>(n) / static_cast<double>(len)));
}

/// Over n in [0, len): |y - (y_old + c(n)(y_new - y_old))| at sample `from` + n; returns the worst.
double worstBlendError(const std::vector<float>& y, const std::vector<float>& yOld, const std::vector<float>& yNew,
                       std::size_t from, std::size_t len) {
    double worst = 0.0;
    for (std::size_t n = 0; n < len; ++n) {
        const double o = static_cast<double>(yOld[from + n]);
        const double nw = static_cast<double>(yNew[from + n]);
        const double expected = o + xfadeWeight(n, len) * (nw - o);
        worst = std::max(worst, std::abs(static_cast<double>(y[from + n]) - expected));
    }
    return worst;
}

double peakAbs(const std::vector<float>& x, std::size_t from, std::size_t to) {
    double p = 0.0;
    for (std::size_t i = from; i < to; ++i)
        p = std::max(p, std::abs(static_cast<double>(x[i])));
    return p;
}

}  // namespace

TEST_CASE("AnchorOscillator_WaveformCrossfade", "[systems][profundum][anchor]") {
    constexpr double kFs = 48000.0;
    constexpr std::size_t kSwitch = 48000;
    constexpr std::size_t kCompare = 4800;
    const auto nx = static_cast<std::size_t>(std::max<long long>(
        1, std::llround(static_cast<double>(AnchorOscillator::kWaveformCrossfadeMs) * kFs / 1000.0)));
    REQUIRE(nx == 240);
    const std::size_t total = kSwitch + 2 * nx + 1 + kCompare;

    // Reference renders of each waveform alone (same calls, same phi). kAllWavesT011 is in enum order.
    std::array<std::vector<float>, kAllWavesT011.size()> ref;
    for (std::size_t i = 0; i < kAllWavesT011.size(); ++i)
        ref[i] = renderXfade(kAllWavesT011[i], total, {});
    const auto refOf = [&](AW w) -> const std::vector<float>& { return ref[static_cast<std::size_t>(w)]; };

    SECTION("every ordered pair: raised-cosine blend, then bitwise the new waveform") {
        int pairs = 0;
        for (const AW from : kAllWavesT011) {
            for (const AW to : kAllWavesT011) {
                if (from == to)
                    continue;
                ++pairs;
                const std::vector<float> y = renderXfade(from, total, {{kSwitch, XfadeEvent::Kind::Wave, to}});
                INFO("pair " << static_cast<int>(from) << " -> " << static_cast<int>(to));
                // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) - intentional bit-exact check
                REQUIRE(std::memcmp(y.data(), refOf(from).data(), kSwitch * sizeof(float)) == 0);
                const double err = worstBlendError(y, refOf(from), refOf(to), kSwitch, nx);
                const double peakXf = peakAbs(y, kSwitch, kSwitch + nx);
                const double peakRefs = std::max(peakAbs(refOf(from), kSwitch - 4800, total),
                                                 peakAbs(refOf(to), kSwitch - 4800, total));
                std::printf("FR-014 crossfade %d -> %d: worst blend error %.3e (bar 1e-6), peak %.6f vs refs %.6f\n",
                            static_cast<int>(from), static_cast<int>(to), err, peakXf, peakRefs);
                CHECK(err <= 1e-6);
                CHECK(peakXf <= peakRefs + 1e-6);
                // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) - intentional bit-exact check
                CHECK(std::memcmp(y.data() + kSwitch + nx + 1, refOf(to).data() + kSwitch + nx + 1,
                                  kCompare * sizeof(float))
                      == 0);
            }
        }
        REQUIRE(pairs == 20);
    }

    SECTION("E-7: a switch during a crossfade is queued (latest wins)") {
        struct Triple {
            AW a, b, c;
        };
        for (const Triple t : {Triple{AW::Sine, AW::Triangle, AW::RoundedSine},
                               Triple{AW::AsymmetricSine, AW::OctaveDownSine, AW::Sine},
                               Triple{AW::Triangle, AW::AsymmetricSine, AW::Triangle}}) {
            // The second switch arrives at n = 100, during the first crossfade: it is queued and
            // starts when the first crossfade completes (n = nx).
            const std::vector<float> y = renderXfade(
                t.a, total, {{kSwitch, XfadeEvent::Kind::Wave, t.b}, {kSwitch + 100, XfadeEvent::Kind::Wave, t.c}});
            INFO("triple " << static_cast<int>(t.a) << " -> " << static_cast<int>(t.b) << " -> "
                           << static_cast<int>(t.c));
            const double err1 = worstBlendError(y, refOf(t.a), refOf(t.b), kSwitch, nx);
            const double err2 = worstBlendError(y, refOf(t.b), refOf(t.c), kSwitch + nx, nx);
            std::printf("E-7 queued %d -> %d -> %d: first blend error %.3e, queued blend error %.3e (bar 1e-6)\n",
                        static_cast<int>(t.a), static_cast<int>(t.b), static_cast<int>(t.c), err1, err2);
            CHECK(err1 <= 1e-6);
            CHECK(err2 <= 1e-6);
            const std::size_t settled = kSwitch + 2 * nx + 1;
            CHECK(std::memcmp(y.data() + settled, refOf(t.c).data() + settled, (total - settled) * sizeof(float))
                  == 0);
        }
    }

    SECTION("a Reset noteOn mid-crossfade completes it") {
        // Sine -> rounded at kSwitch; a Reset noteOn (same f0) at n = 100. From the noteOn the render is
        // bit-identical to a fresh instance started on the rounded sine (the next block renders the new
        // waveform only).
        const std::size_t resetAt = kSwitch + 100;
        const std::vector<float> y = renderXfade(AW::Sine, total,
                                                 {{kSwitch, XfadeEvent::Kind::Wave, AW::RoundedSine},
                                                  {resetAt, XfadeEvent::Kind::ResetNoteOn, AW::RoundedSine}});
        const std::vector<float> fresh = renderXfade(AW::RoundedSine, total - resetAt, {});
        REQUIRE(y[resetAt] == 0.0f);
        REQUIRE(std::memcmp(y.data() + resetAt, fresh.data(), (total - resetAt) * sizeof(float)) == 0);
    }
}

// =============================================================================
// T012: level and a1 smoothing (FR-020, FR-023; plan S4.9; tasks.md deviation 6)
// =============================================================================

namespace {

constexpr double kFsT012 = 48000.0;
constexpr std::size_t kStepAtT012 = 48000;     // the step lands 1 s into a steady note
constexpr std::size_t kTotalT012 = 96000;

/// Sine, LOCK, C2, 48 kHz, 1-sample blocks. A Reset noteOn with level `levelFrom` and a1 `a1From`; at
/// sample kStepAtT012 the level target becomes `levelTo` (setControls, latched at that block's offset 0)
/// and the a1 target becomes `a1To` (setCoreH1Amplitude). An unchanged value issues no call.
std::vector<float> renderGainStepT012(float levelFrom, float levelTo, float a1From, float a1To) {
    AnchorOscillator a;
    a.prepare(kFsT012);
    AnchorOscillator::Controls c{};
    c.waveform = AW::Sine;
    c.mode = AM::Lock;
    c.level = levelFrom;
    a.setControls(c);
    a.setCoreH1Amplitude(a1From);
    a.setRetriggerPhase(ARP::Reset);
    a.noteOn(PT::anchorMidiHz(36));
    const PT::AnchorRender pre = PT::renderAnchor(a, kStepAtT012, 1);
    if (levelTo != levelFrom) {
        c.level = levelTo;
        a.setControls(c);
    }
    if (a1To != a1From)
        a.setCoreH1Amplitude(a1To);
    const PT::AnchorRender post = PT::renderAnchor(a, kTotalT012 - kStepAtT012, 1);
    REQUIRE(a.stateFinite());
    std::vector<float> y = pre.L;
    y.insert(y.end(), post.L.begin(), post.L.end());
    return y;
}

std::size_t countClicksT012(const std::vector<float>& y) {
    Krate::DSP::TestUtils::ClickDetectorConfig cfg{};   // defaults (artifact_detection.h:38-45)
    cfg.sampleRate = static_cast<float>(kFsT012);
    Krate::DSP::TestUtils::ClickDetector detector(cfg);
    detector.prepare();
    return detector.detect(y.data(), y.size()).size();
}

}  // namespace

TEST_CASE("AnchorOscillator_LevelAndA1Smoothing", "[systems][profundum][anchor]") {
    const float a1Max = AnchorOscillator::kMaxCoreH1Amplitude;
    // The smoother's first step per unit change: 1 − exp(−5000/(kLevelSmoothMs·fs)) (smoother.h:77-92).
    const double firstStep =
        1.0 - std::exp(-5000.0 / (static_cast<double>(AnchorOscillator::kLevelSmoothMs) * kFsT012));
    INFO("smoother first step " << firstStep);
    REQUIRE(std::abs(firstStep - 0.020618) <= 1e-6);

    // D_max / P_max: 1 s steady render at level 1, LOCK, a1 = kMaxCoreH1Amplitude (carry k·a1/√2).
    const std::vector<float> steady = renderGainStepT012(1.0f, 1.0f, a1Max, a1Max);
    const double dMax = maxAbsDelta(steady, kStepAtT012, kTotalT012);
    const double pMax = peakAbs(steady, kStepAtT012, kTotalT012);
    REQUIRE(dMax > 0.0);
    REQUIRE(pMax > 0.0);

    struct Step {
        const char* name;
        float levelFrom, levelTo, a1From, a1To;
    };
    const std::array<Step, 4> steps{{
        {"level 0 -> 1", 0.0f, 1.0f, a1Max, a1Max},
        {"level 1 -> 0", 1.0f, 0.0f, a1Max, a1Max},
        {"a1 0 -> max", 1.0f, 1.0f, 0.0f, a1Max},
        {"a1 max -> 0", 1.0f, 1.0f, a1Max, 0.0f},
    }};
    for (const Step& s : steps) {
        const std::vector<float> y = renderGainStepT012(s.levelFrom, s.levelTo, s.a1From, s.a1To);
        // (i)/(ii): G_max = 1 (the largest level × g reached); S_max is the smoother's first step of
        // the change, normalised to a1 = kMaxCoreH1Amplitude for the a1 arms (P_max carries a1max).
        const double deltaL = std::abs(static_cast<double>(s.levelTo) - static_cast<double>(s.levelFrom));
        const double deltaA = std::abs(static_cast<double>(s.a1To) - static_cast<double>(s.a1From))
                              / static_cast<double>(a1Max);
        const double sMax = firstStep * std::max(deltaL, deltaA);
        const double bound = 1.1 * (1.0 * dMax + sMax * pMax);
        const double worst = maxAbsDelta(y, 1, y.size());
        const std::size_t clicks = countClicksT012(y);
        std::printf("FR-020/FR-023 %s: max|dy| %.6e, bound %.6e (D_max %.6e, P_max %.6f, S_max %.6f), "
                    "ratio %.4f; clicks %zu\n",
                    s.name, worst, bound, dMax, pMax, sMax, worst / bound, clicks);
        INFO(s.name);
        CHECK(worst <= bound);
        CHECK(clicks == 0);   // (iii)
    }

    // (iii) positive control: the same render with a spliced instantaneous level step 0 -> 1.
    std::vector<float> spliced = steady;
    std::fill(spliced.begin(), spliced.begin() + static_cast<std::ptrdiff_t>(kStepAtT012), 0.0f);
    const std::size_t controlClicks = countClicksT012(spliced);
    std::printf("FR-020 positive control (instantaneous level step 0 -> 1): clicks %zu (bar >= 1)\n", controlClicks);
    CHECK(controlClicks >= 1);
}

// =============================================================================
// T013: mode envelope g — segment program, raised cosine, Hermite bridge, snap / bridged arms,
// mode-switch bridge, rearm semantics (plan S4.6-S4.7; FR-040-FR-044, FR-048; SC-004, SC-005,
// SC-006, SC-017, SC-027; E-6, E-8, E-13; plan S1 C-3, C-8, C-12)
// =============================================================================
// Every envelope render runs in 1-sample blocks and reads g from modeGain() (plan S1 C-4: double).
// The expected program of every transition comes from a test-side model of plan S4.7 (below) whose
// bridge length is the plan S1 C-3 rule re-derived here: the largest N <= N_nominal whose cubic's
// interior extrema (textbook quadratic roots) stay in [0, max(B, g0)], by integer bisection.

namespace {

constexpr double kFsT013 = 48000.0;
using SegKindT013 = PT::AnchorSegmentSpec::Kind;

std::size_t samplesT013(float ms, double fs = kFsT013) {   // tasks.md rule 10: N = max(1, round(ms·fs/1000))
    return static_cast<std::size_t>(std::max<long long>(1, std::llround(static_cast<double>(ms) * fs / 1000.0)));
}

double boostT013(float db) {
    return std::pow(10.0, static_cast<double>(db) / 20.0);
}

double dbT013(double x) {
    return 20.0 * std::log10(x);
}

bool sameBitsT013(double a, double b) {
    return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}

AnchorOscillator::Controls envControlsT013(AM mode, float boostDb, float punchMs, float ghostMs, float level = 1.0f,
                                           AW wave = AW::Sine) {
    AnchorOscillator::Controls c{};
    c.waveform = wave;
    c.mode = mode;
    c.level = level;
    c.punchBoostDb = boostDb;
    c.punchTimeMs = punchMs;
    c.ghostTimeMs = ghostMs;
    return c;
}

void appendT013(PT::AnchorRender& acc, const PT::AnchorRender& part) {
    acc.L.insert(acc.L.end(), part.L.begin(), part.L.end());
    acc.R.insert(acc.R.end(), part.R.begin(), part.R.end());
    acc.g.insert(acc.g.end(), part.g.begin(), part.g.end());
    acc.phase.insert(acc.phase.end(), part.phase.begin(), part.phase.end());
    acc.inc.insert(acc.inc.end(), part.inc.begin(), part.inc.end());
}

// ---- test-side model of plan S4.7 ----

struct ModelSegT013 {
    SegKindT013 kind;
    double g0, a, gp0, g1;   // Cosine: g0 + a·½(1 − cos(π·n/len)); Hermite: (g0, gp0 per sample) -> (g1, 0)
    std::size_t start, len;  // absolute start sample; full analytic length (0 for Hold)
};

double modelValueT013(const ModelSegT013& s, std::size_t n) {
    switch (s.kind) {
        case SegKindT013::Hold:
            return s.g0;
        case SegKindT013::Cosine:
            return PT::anchorRaisedCosine(s.g0, s.a, n, s.len);
        case SegKindT013::Hermite:
            return PT::anchorHermite(s.g0, s.gp0, s.g1, n, s.len);
    }
    return s.g0;
}

/// Hermite derivative per sample at continuous t (m0 = len·gp0).
double hermiteSlopeAtT013(const ModelSegT013& s, double t) {
    const double L = static_cast<double>(s.len);
    const double m0 = L * s.gp0;
    const double d = s.g1 - s.g0;
    return ((3.0 * m0 - 6.0 * d) * t * t + (6.0 * d - 4.0 * m0) * t + m0) / L;
}

double modelSlopeT013(const ModelSegT013& s, std::size_t n) {
    const double L = static_cast<double>(s.len);
    switch (s.kind) {
        case SegKindT013::Hold:
            return 0.0;
        case SegKindT013::Cosine:
            return s.a * PT::kAnchorPiD / (2.0 * L) * std::sin(PT::kAnchorPiD * static_cast<double>(n) / L);
        case SegKindT013::Hermite:
            return hermiteSlopeAtT013(s, static_cast<double>(n) / L);
    }
    return 0.0;
}

/// SC-006(c) S_max of one segment: raised cosine |A|·π/(2N); Hermite the cubic's analytic peak slope
/// (at t = 0, t = 1 or the vertex of the quadratic g'); Hold 0.
double modelPeakSlopeT013(const ModelSegT013& s) {
    switch (s.kind) {
        case SegKindT013::Hold:
            return 0.0;
        case SegKindT013::Cosine:
            return std::abs(s.a) * PT::kAnchorPiD / (2.0 * static_cast<double>(s.len));
        case SegKindT013::Hermite: {
            const double L = static_cast<double>(s.len);
            const double m0 = L * s.gp0;
            const double d = s.g1 - s.g0;
            const double qa = 3.0 * m0 - 6.0 * d;
            const double qb = 6.0 * d - 4.0 * m0;
            double peak = std::max(std::abs(hermiteSlopeAtT013(s, 0.0)), std::abs(hermiteSlopeAtT013(s, 1.0)));
            if (qa != 0.0) {
                const double tv = -qb / (2.0 * qa);
                if (tv > 0.0 && tv < 1.0)
                    peak = std::max(peak, std::abs(hermiteSlopeAtT013(s, tv)));
            }
            return peak;
        }
    }
    return 0.0;
}

/// Plan S1 C-3 rule, re-derived: the interior extrema of the cubic (roots in (0, 1) of
/// (3m0 − 6Δ)t² + (6Δ − 4m0)t + m0, textbook formula) lie in [0, hi].
bool bridgeFitsT013(double g0, double gp0, double g1, std::size_t len, double hi) {
    const double L = static_cast<double>(len);
    const double m0 = L * gp0;
    const double d = g1 - g0;
    const double qa = 3.0 * m0 - 6.0 * d;
    const double qb = 6.0 * d - 4.0 * m0;
    const double qc = m0;
    std::vector<double> roots;
    if (qa == 0.0) {
        if (qb != 0.0)
            roots.push_back(-qc / qb);
    } else {
        const double disc = qb * qb - 4.0 * qa * qc;
        if (disc >= 0.0) {
            const double sq = std::sqrt(disc);
            roots.push_back((-qb - sq) / (2.0 * qa));
            roots.push_back((-qb + sq) / (2.0 * qa));
        }
    }
    for (const double t : roots) {
        if (t <= 0.0 || t >= 1.0)
            continue;
        const double t2 = t * t;
        const double t3 = t2 * t;
        const double v = (2.0 * t3 - 3.0 * t2 + 1.0) * g0 + (t3 - 2.0 * t2 + t) * m0 + (-2.0 * t3 + 3.0 * t2) * g1;
        if (v < 0.0 || v > hi)
            return false;
    }
    return true;
}

std::size_t bridgeLenT013(double g0, double gp0, double g1, std::size_t nominal, double hi) {
    if (bridgeFitsT013(g0, gp0, g1, nominal, hi))
        return nominal;
    std::size_t good = 1;
    std::size_t bad = nominal;
    while (bad - good > 1) {
        const std::size_t mid = good + (bad - good) / 2;
        if (bridgeFitsT013(g0, gp0, g1, mid, hi))
            good = mid;
        else
            bad = mid;
    }
    return good;
}

/// The plan S4.7 program table, applied to absolute sample positions. segs holds the realised
/// history followed by the program in force (each segment ends where the next starts).
struct EnvModelT013 {
    double fs = kFsT013;
    AnchorOscillator::Controls pending{};
    AnchorOscillator::Controls active{};
    bool sounding = false;
    std::vector<ModelSegT013> segs;
    std::vector<std::size_t> bridgeLens;   // N_b of every bridge built (recorded beside plan S9.6)

    [[nodiscard]] std::size_t segAt(std::size_t n) const {   // segs is never empty after the first apply
        std::size_t k = 0;
        for (std::size_t i = 0; i < segs.size(); ++i) {
            if (segs[i].start <= n)
                k = i;
        }
        return k;
    }
    [[nodiscard]] double valueAt(std::size_t n) const {
        const ModelSegT013& s = segs[segAt(n)];
        return modelValueT013(s, n - s.start);
    }
    [[nodiscard]] double slopeAt(std::size_t n) const {
        const ModelSegT013& s = segs[segAt(n)];
        return modelSlopeT013(s, n - s.start);
    }
    void cut(std::size_t s) {   // drops every segment that would start at or after s
        while (!segs.empty() && segs.back().start >= s)
            segs.pop_back();
    }
    void push(SegKindT013 kind, double g0, double a, double gp0, double g1, std::size_t start, std::size_t len) {
        segs.push_back(ModelSegT013{kind, g0, a, gp0, g1, start, len});
    }
    void pushCos(double g0, double g1, std::size_t len, std::size_t start) {
        push(SegKindT013::Cosine, g0, g1 - g0, 0.0, g1, start, len);
    }
    void pushHold(double v, std::size_t start) { push(SegKindT013::Hold, v, 0.0, 0.0, v, start, 0); }
    std::size_t pushBridge(double g0, double gp0, double g1, std::size_t nominal, std::size_t start) {
        const double hi = std::max(boostT013(active.punchBoostDb), g0);
        const std::size_t nb = bridgeLenT013(g0, gp0, g1, nominal, hi);
        bridgeLens.push_back(nb);
        push(SegKindT013::Hermite, g0, 0.0, gp0, g1, start, nb);
        return nb;
    }

    void snap(std::size_t s) {
        cut(s);
        const std::size_t nr = samplesT013(AnchorOscillator::kPunchRiseMs, fs);
        switch (active.mode) {
            case AM::Lock:
            case AM::Follow:
                pushHold(1.0, s);
                return;
            case AM::Punch: {
                const double b = boostT013(active.punchBoostDb);
                const std::size_t nt = samplesT013(active.punchTimeMs, fs);
                pushCos(1.0, b, nr, s);
                pushCos(b, 1.0, nt, s + nr);
                pushHold(1.0, s + nr + nt);
                return;
            }
            case AM::Ghost: {
                const std::size_t ng = samplesT013(active.ghostTimeMs, fs);
                pushCos(0.0, 1.0, ng, s);
                pushHold(1.0, s + ng);
                return;
            }
        }
    }
    void rest(std::size_t s, double g0, double gp) {
        if (g0 == 1.0 && gp == 0.0) {
            pushHold(1.0, s);
            return;
        }
        const std::size_t nb = pushBridge(g0, gp, 1.0, samplesT013(AnchorOscillator::kModeBridgeMs, fs), s);
        pushHold(1.0, s + nb);
    }
    void bridged(std::size_t s) {
        const double g0 = valueAt(s);
        const double gp = slopeAt(s);
        cut(s);
        switch (active.mode) {
            case AM::Lock:
            case AM::Follow:
                rest(s, g0, gp);
                return;
            case AM::Punch: {
                const double b = boostT013(active.punchBoostDb);
                const std::size_t nt = samplesT013(active.punchTimeMs, fs);
                std::size_t nb = 0;
                if (g0 != b || gp != 0.0)
                    nb = pushBridge(g0, gp, b, samplesT013(AnchorOscillator::kPunchRiseMs, fs), s);
                pushCos(b, 1.0, nt, s + nb);
                pushHold(1.0, s + nb + nt);
                return;
            }
            case AM::Ghost: {
                const std::size_t ng = samplesT013(active.ghostTimeMs, fs);
                std::size_t nb = 0;
                if (g0 != 0.0 || gp != 0.0)
                    nb = pushBridge(g0, gp, 0.0, samplesT013(AnchorOscillator::kGhostDipMs, fs), s);
                pushCos(0.0, 1.0, ng, s + nb);
                pushHold(1.0, s + nb + ng);
                return;
            }
        }
    }
    /// One block start at sample s (header order, plan S4.4 / S4.6; C-8: a re-arm wins).
    void apply(std::size_t s, const AnchorOscillator::Controls* controls, bool noteOn, ARP policy, bool rearm) {
        if (controls != nullptr)
            pending = *controls;
        const AM previous = active.mode;
        active = pending;
        if (sounding && active.mode != previous) {   // FR-044 mode switch
            const double g0 = valueAt(s);
            const double gp = slopeAt(s);
            cut(s);
            rest(s, g0, gp);
        }
        if (!noteOn)
            return;
        const bool fromSilence = !sounding;
        const bool arm = rearm || fromSilence;
        if (policy == ARP::Reset || fromSilence) {
            if (arm)
                snap(s);
        } else if (arm) {
            bridged(s);
        }
        sounding = true;
    }
};

struct EnvEventT013 {
    std::size_t at;
    bool setControls;
    AnchorOscillator::Controls controls;
    bool noteOn;
    ARP policy;
    bool rearm;
};

EnvEventT013 armEventT013(std::size_t at, ARP policy, bool rearm) {
    return EnvEventT013{at, false, AnchorOscillator::Controls{}, true, policy, rearm};
}

EnvEventT013 controlsEventT013(std::size_t at, const AnchorOscillator::Controls& c) {
    return EnvEventT013{at, true, c, false, ARP::Reset, false};
}

struct EnvRunT013 {
    PT::AnchorRender r;
    EnvModelT013 model;
    std::vector<double> modelG;
};

/// T014: the held follow ratio of every envelope-fixture voice, so a FOLLOW row (and every switch to
/// or from FOLLOW) runs FOLLOW as itself; r_eff = 1 in every other mode, so no other row changes.
constexpr float kEnvFollowRatio = 1.5f;

/// A Reset noteOn(f0) with `initial` at sample 0 (held ratio kEnvFollowRatio), then `events` (each
/// applied before its sample, in list order), `total` samples in 1-sample blocks; the model runs the
/// same calls.
EnvRunT013 runEnvT013(const AnchorOscillator::Controls& initial, float f0, const std::vector<EnvEventT013>& events,
                      std::size_t total, double fs = kFsT013) {
    EnvRunT013 run;
    run.model.fs = fs;
    AnchorOscillator a;
    a.prepare(fs);
    a.setFollowRatio(kEnvFollowRatio);
    a.setControls(initial);
    a.setRetriggerPhase(ARP::Reset);
    a.noteOn(f0);
    run.model.apply(0, &initial, true, ARP::Reset, true);
    std::size_t pos = 0;
    const auto renderTo = [&](std::size_t to) {
        if (to > pos) {
            appendT013(run.r, PT::renderAnchor(a, to - pos, 1, nullptr, nullptr, true));
            pos = to;
        }
    };
    for (const EnvEventT013& e : events) {
        REQUIRE(e.at > 0);
        REQUIRE(e.at >= pos);
        REQUIRE(e.at < total);
        renderTo(e.at);
        if (e.setControls)
            a.setControls(e.controls);
        if (e.noteOn) {
            a.setRetriggerPhase(e.policy);
            a.noteOn(f0, e.rearm);
        }
        run.model.apply(e.at, e.setControls ? &e.controls : nullptr, e.noteOn, e.policy, e.rearm);
    }
    renderTo(total);
    REQUIRE(run.r.g.size() == total);
    REQUIRE(a.stateFinite());
    run.modelG.resize(total);
    for (std::size_t i = 0; i < total; ++i)
        run.modelG[i] = run.model.valueAt(i);
    return run;
}

/// The checker's segment list. A segment cut short by a later event spans [start, next start]; it
/// is passed as a proxy raised cosine over its realised span whose |a|·π²/(2·span²) equals the full
/// segment's analytic bound (anchorSegmentD2Bound derives the bound from len, so the realised span
/// cannot carry the full segment's N itself).
std::vector<PT::AnchorSegmentSpec> specsT013(const EnvModelT013& m, std::size_t total) {
    std::vector<PT::AnchorSegmentSpec> out;
    for (std::size_t i = 0; i < m.segs.size(); ++i) {
        const ModelSegT013& s = m.segs[i];
        const std::size_t end = i + 1 < m.segs.size() ? m.segs[i + 1].start : total - 1;
        if (end <= s.start)
            continue;
        const std::size_t span = end - s.start;
        if (s.kind == SegKindT013::Hold || span == s.len) {
            out.push_back(PT::AnchorSegmentSpec{s.kind, s.g0, s.a, s.gp0, s.g1, s.start, span});
            continue;
        }
        const PT::AnchorSegmentSpec full{s.kind, s.g0, s.a, s.gp0, s.g1, s.start, s.len};
        const double bound = PT::anchorSegmentD2Bound(full);
        const double sp = static_cast<double>(span);
        const double aProxy = bound * 2.0 * sp * sp / (PT::kAnchorPiD * PT::kAnchorPiD);
        out.push_back(PT::AnchorSegmentSpec{SegKindT013::Cosine, s.g0, aProxy, 0.0, s.g0 + aProxy, s.start, span});
    }
    return out;
}

/// The end of the last non-Hold segment (the transition's end), capped at total.
std::size_t programEndT013(const EnvModelT013& m, std::size_t total) {
    std::size_t end = 0;
    for (const ModelSegT013& s : m.segs) {
        if (s.kind != SegKindT013::Hold)
            end = std::max(end, s.start + s.len);
    }
    return std::min(end, total);
}

std::string bridgeListT013(const EnvModelT013& m) {
    std::string out;
    for (const std::size_t nb : m.bridgeLens) {
        if (!out.empty())
            out += ",";
        out += std::to_string(nb);
    }
    return out.empty() ? std::string("none") : out;
}

// ---- SC-006 fixture (C2, 48 kHz, B = 12 dB) ----

struct EnvCaseT013 {
    std::string name;
    AnchorOscillator::Controls initial;
    std::vector<EnvEventT013> events;
    std::size_t total;
    std::size_t focus;          // the transition's first sample
    bool resetsSounding;        // a Reset noteOn on a sounding voice: not a (b)/(c) transition (FR-006)
    bool expectUnchanged;       // rearm = false: g bit-identical to the render without the event
};

const char* modeNameT013(AM m) {
    switch (m) {
        case AM::Lock:
            return "Lock";
        case AM::Follow:
            return "Follow";
        case AM::Punch:
            return "Punch";
        case AM::Ghost:
            return "Ghost";
    }
    return "?";
}

std::vector<EnvCaseT013> envFixtureT013() {
    constexpr float kBoost = 12.0f;
    struct TimesT013 {
        float punchMs, ghostMs;
    };
    std::vector<EnvCaseT013> cases;
    for (const TimesT013 tm : {TimesT013{5.0f, 1.0f}, TimesT013{250.0f, 100.0f}}) {
        const std::size_t nr = samplesT013(AnchorOscillator::kPunchRiseMs);
        const std::size_t nt = samplesT013(tm.punchMs);
        const std::size_t ng = samplesT013(tm.ghostMs);
        const std::size_t nd = samplesT013(AnchorOscillator::kGhostDipMs);
        const std::size_t tail = nr + nt + ng + nd + samplesT013(AnchorOscillator::kModeBridgeMs) + 512;
        const auto ctl = [&](AM m) { return envControlsT013(m, kBoost, tm.punchMs, tm.ghostMs); };
        const std::string tag = " [punch " + std::to_string(static_cast<int>(tm.punchMs)) + " ms, ghost "
                                + std::to_string(static_cast<int>(tm.ghostMs)) + " ms]";

        struct PosT013 {
            const char* name;
            std::vector<EnvEventT013> pre;
            std::size_t at;
        };
        const auto positions = [&](AM m) {
            if (m == AM::Ghost) {   // mid-fall of GHOST = mid-dip of a bridged arm from steady state
                return std::vector<PosT013>{
                    {"mid-rise", {}, ng / 2},
                    {"mid-fall", {armEventT013(ng + 64, ARP::FreeRunning, true)}, ng + 64 + nd / 2},
                    {"steady", {}, ng + 64},
                };
            }
            return std::vector<PosT013>{
                {"mid-rise", {}, nr / 2},
                {"mid-fall", {}, nr + nt / 2},
                {"steady", {}, nr + nt + 64},
            };
        };
        const auto add = [&](std::string name, AM initialMode, std::vector<EnvEventT013> events, std::size_t focus,
                             bool resetsSounding, bool expectUnchanged, float level = 1.0f) {
            const std::size_t last = events.empty() ? 0 : events.back().at;
            AnchorOscillator::Controls init = ctl(initialMode);
            init.level = level;
            cases.push_back(EnvCaseT013{std::move(name) + tag, init, std::move(events), last + tail, focus,
                                        resetsSounding, expectUnchanged});
        };

        add("PUNCH snap arm", AM::Punch, {}, 0, false, false);
        add("GHOST snap arm", AM::Ghost, {}, 0, false, false);
        for (const AM m : {AM::Punch, AM::Ghost}) {
            for (const PosT013& p : positions(m)) {
                std::vector<EnvEventT013> ev = p.pre;
                ev.push_back(armEventT013(p.at, ARP::FreeRunning, true));
                add(std::string(modeNameT013(m)) + " bridged arm at " + p.name, m, std::move(ev), p.at, false, false);
            }
        }
        for (const AM m : {AM::Punch, AM::Ghost}) {
            const std::size_t mid = m == AM::Punch ? nr + nt / 2 : ng / 2;
            for (const ARP pol : {ARP::Reset, ARP::FreeRunning}) {
                add(std::string(modeNameT013(m)) + " rearm=false " + (pol == ARP::Reset ? "Reset" : "FreeRunning")
                        + " mid-envelope",
                    m, {armEventT013(mid, pol, false)}, mid, pol == ARP::Reset, true);
            }
        }
        for (const AM from : {AM::Lock, AM::Follow, AM::Punch, AM::Ghost}) {   // T014 adds Follow
            for (const AM to : {AM::Lock, AM::Follow, AM::Punch, AM::Ghost}) {
                if (from == to)
                    continue;
                for (const PosT013& p : positions(from)) {
                    std::vector<EnvEventT013> ev = p.pre;
                    ev.push_back(controlsEventT013(p.at, ctl(to)));
                    add(std::string("mode ") + modeNameT013(from) + " -> " + modeNameT013(to) + " at " + p.name, from,
                        std::move(ev), p.at, false, false);
                }
            }
        }
        add("level-0 PUNCH (E-13)", AM::Punch, {}, 0, false, false, 0.0f);
    }
    return cases;
}

}  // namespace

// =============================================================================
// SC-004 (plan S1 C-12): PUNCH boost and duration
// =============================================================================

TEST_CASE("AnchorOscillator_PunchBoostAndDuration", "[systems][profundum][anchor]") {
    const std::size_t nr = samplesT013(AnchorOscillator::kPunchRiseMs);
    REQUIRE(nr == 48);
    for (const int midi : {28, 48}) {   // E1, C3
        const float f0 = PT::anchorMidiHz(midi);
        for (const float boost : {3.0f, 6.0f, 12.0f}) {
            for (const float pt : {5.0f, 40.0f, 250.0f}) {
                const std::size_t nt = samplesT013(pt);
                const std::size_t total = nr + nt + 2400;
                const auto render = [&](AM mode) {
                    AnchorOscillator a;
                    a.prepare(kFsT013);
                    a.setControls(envControlsT013(mode, boost, pt, 15.0f));
                    a.setRetriggerPhase(ARP::Reset);
                    a.noteOn(f0);
                    return PT::renderAnchor(a, total, 1, nullptr, nullptr, true);
                };
                const PT::AnchorRender punch = render(AM::Punch);
                const PT::AnchorRender lock = render(AM::Lock);
                const double B = boostT013(boost);
                INFO("midi " << midi << " boost " << boost << " dB punchTimeMs " << pt);

                // (a) peak of g: observer.
                const auto peakIt = std::max_element(punch.g.begin(), punch.g.end());
                const auto peakIdx = static_cast<std::size_t>(peakIt - punch.g.begin());
                const double peakDb = dbT013(*peakIt / B);

                // (a) gain trace y_mode / y_lock where |y_lock| >= 0.1·peak, and (c) its match to g.
                double peakLock = 0.0;
                for (const float y : lock.L)
                    peakLock = std::max(peakLock, std::abs(static_cast<double>(y)));
                REQUIRE(peakLock > 0.0);
                double traceMax = 0.0;
                double worstC = 0.0;
                std::size_t evaluated = 0;
                for (std::size_t i = 0; i < total; ++i) {
                    const auto yLock = static_cast<double>(lock.L[i]);
                    if (std::abs(yLock) < 0.1 * peakLock)
                        continue;
                    ++evaluated;
                    const double tr = static_cast<double>(punch.L[i]) / yLock;
                    traceMax = std::max(traceMax, tr);
                    worstC = std::max(worstC, std::abs(dbT013(tr / punch.g[i])));
                }
                REQUIRE(evaluated > 0);

                // (b) the return: the first sample after the peak with g == 1.0 bitwise.
                std::size_t ret = total;
                for (std::size_t i = peakIdx + 1; i < total; ++i) {
                    if (sameBitsT013(punch.g[i], 1.0)) {
                        ret = i;
                        break;
                    }
                }
                REQUIRE(ret < total);
                double worstAfter = 0.0;
                for (std::size_t i = ret; i < total; ++i)
                    worstAfter = std::max(worstAfter, std::abs(dbT013(punch.g[i])));
                const double midDb = dbT013(punch.g[nr + nt / 2]);

                // (b) the whole trace from the arm to the return is the D-4 raised-cosine pair.
                double worstRc = 0.0;
                for (std::size_t i = 0; i <= nr + nt; ++i) {
                    const double expected = i <= nr ? PT::anchorRaisedCosine(1.0, B - 1.0, i, nr)
                                                    : PT::anchorRaisedCosine(B, 1.0 - B, i - nr, nt);
                    worstRc = std::max(worstRc, std::abs(punch.g[i] - expected));
                }

                std::printf("SC-004 midi %d boost %4.1f dB time %5.1f ms: peak %+.5f dB (observer) %+.5f dB (trace) "
                            "at %zu; return at %zu (expected %zu); after %.2e dB; mid %+.3f dB; trace vs g %.2e dB; "
                            "vs closed form %.2e\n",
                            midi, static_cast<double>(boost), static_cast<double>(pt), peakDb, dbT013(traceMax / B),
                            peakIdx, ret, nr + nt, worstAfter, midDb, worstC, worstRc);
                CHECK(std::abs(peakDb) <= 0.1);
                CHECK(std::abs(dbT013(traceMax / B)) <= 0.1);
                CHECK(worstC <= 0.05);
                CHECK(std::abs(static_cast<long long>(ret) - static_cast<long long>(nr + nt)) <= 1);
                CHECK(punch.g[ret - 1] > 1.0);
                CHECK(punch.g[nr + nt - 1] > 1.0);
                CHECK(worstAfter <= 0.01);
                CHECK(punch.g[nr + nt / 2] > std::pow(10.0, 0.1 / 20.0));
                CHECK(worstRc <= 1e-9);
            }
        }
    }
}

// =============================================================================
// SC-005: GHOST emergence on every arm path (P1 Reset, P2 bridged, P3 no re-arm)
// =============================================================================

namespace {

/// SC-005 (a)-(c) on g from t0 (g reaches 0 there) over an emergence of N samples.
void checkEmergenceShapeT013(const char* label, const std::vector<double>& g, std::size_t t0, std::size_t N) {
    INFO(label << " t0 " << t0 << " N " << N);
    REQUIRE(t0 + N + 2 <= g.size());
    CHECK(g[t0] == 0.0);   // (a) g(0) = 0
    double worstEarly = -1000.0;
    for (std::size_t n = 0; static_cast<double>(n) <= 0.1 * static_cast<double>(N); ++n) {
        if (g[t0 + n] > 0.0)
            worstEarly = std::max(worstEarly, dbT013(g[t0 + n]));
    }
    CHECK(worstEarly <= -30.0);   // (a) <= −30 dB for t <= 0.1·T
    double worstLate = 0.0;
    for (std::size_t i = t0 + N + 1; i < g.size(); ++i)
        worstLate = std::max(worstLate, std::abs(dbT013(g[i])));
    CHECK(worstLate <= 0.01);   // (b)
    bool monotone = true;
    for (std::size_t n = 1; n <= N; ++n) {
        if (g[t0 + n] < g[t0 + n - 1])
            monotone = false;
    }
    CHECK(monotone);   // (c)
    std::printf("SC-005 %s: worst g(t <= 0.1T) %.2f dB, worst |g| after T %.2e dB, monotone %d\n", label, worstEarly,
                worstLate, monotone ? 1 : 0);
}

/// SC-005 (d): window energy [t0, t0 + N] of y re y_lock against the D-4 closed form.
double checkEmergenceEnergyT013(const char* label, const std::vector<float>& y, const std::vector<float>& yLock,
                                std::size_t t0, std::size_t N) {
    REQUIRE(t0 + N < y.size());
    REQUIRE(t0 + N < yLock.size());
    double num = 0.0;
    double den = 0.0;
    double ana = 0.0;
    for (std::size_t n = 0; n <= N; ++n) {
        const auto yv = static_cast<double>(y[t0 + n]);
        const auto yRef = static_cast<double>(yLock[t0 + n]);
        const double gd = PT::anchorRaisedCosine(0.0, 1.0, n, N);
        num += yv * yv;
        den += yRef * yRef;
        ana += gd * gd * yRef * yRef;
    }
    REQUIRE(den > 0.0);
    const double measured = 10.0 * std::log10(num / den);
    const double analytic = 10.0 * std::log10(ana / den);
    INFO(label);
    CHECK(std::abs(measured - analytic) <= 0.25);
    return measured;
}

}  // namespace

TEST_CASE("AnchorOscillator_GhostEmergence", "[systems][profundum][anchor]") {
    const std::size_t nd = samplesT013(AnchorOscillator::kGhostDipMs);
    REQUIRE(nd == 96);
    for (const int midi : {28, 48}) {   // E1, C3
        const float f0 = PT::anchorMidiHz(midi);
        for (const float gt : {1.0f, 5.0f, 20.0f, 100.0f}) {
            const std::size_t N = samplesT013(gt);
            const AnchorOscillator::Controls ctlG = envControlsT013(AM::Ghost, 6.0f, 40.0f, gt);
            const AnchorOscillator::Controls ctlL = envControlsT013(AM::Lock, 6.0f, 40.0f, gt);
            const std::string tag = "midi " + std::to_string(midi) + " ghost " + std::to_string(static_cast<int>(gt))
                                    + " ms";
            INFO(tag);

            // P1: a Reset noteOn(f, true) after 1 s of a sounding note; t = 0 at the noteOn.
            const std::size_t post = N + 2400;
            const auto renderP1 = [&](const AnchorOscillator::Controls& c, bool withPrior) {
                AnchorOscillator a;
                a.prepare(kFsT013);
                a.setControls(c);
                a.setRetriggerPhase(ARP::Reset);
                if (withPrior) {
                    a.noteOn(f0);
                    (void)PT::renderAnchor(a, 48000, 512);
                }
                a.noteOn(f0, true);
                return PT::renderAnchor(a, post, 1, nullptr, nullptr, true);
            };
            const PT::AnchorRender p1 = renderP1(ctlG, true);
            const PT::AnchorRender fresh = renderP1(ctlG, false);
            const PT::AnchorRender lockP1 = renderP1(ctlL, true);
            CHECK(std::memcmp(p1.L.data(), fresh.L.data(), post * sizeof(float)) == 0);
            CHECK(std::memcmp(p1.g.data(), fresh.g.data(), post * sizeof(double)) == 0);
            const std::string l1 = "P1 " + tag;
            checkEmergenceShapeT013(l1.c_str(), p1.g, 0, N);
            const double e1 = checkEmergenceEnergyT013(l1.c_str(), p1.L, lockP1.L, 0, N);

            // P2: a bridged arm (FreeRunning noteOn(f, true)) at g = 1; t = 0 where the dip reaches 0.
            constexpr std::size_t kArm = 24000;   // >= the 100 ms emergence + margin: g == 1 (Hold)
            const auto renderP2 = [&](const AnchorOscillator::Controls& c) {
                AnchorOscillator a;
                a.prepare(kFsT013);
                a.setControls(c);
                a.setRetriggerPhase(ARP::Reset);
                a.noteOn(f0);
                (void)PT::renderAnchor(a, kArm, 512);
                REQUIRE(a.modeGain() == 1.0);
                a.setRetriggerPhase(ARP::FreeRunning);
                a.noteOn(f0, true);
                return PT::renderAnchor(a, nd + N + 2400, 1, nullptr, nullptr, true);
            };
            const PT::AnchorRender p2 = renderP2(ctlG);
            const PT::AnchorRender lockP2 = renderP2(ctlL);
            std::size_t dipEnd = p2.g.size();
            for (std::size_t i = 0; i < p2.g.size(); ++i) {
                if (p2.g[i] == 0.0) {
                    dipEnd = i;
                    break;
                }
            }
            INFO("P2 dip reaches 0 at " << dipEnd << " samples after the arm (bar " << nd + 1 << ")");
            REQUIRE(dipEnd <= nd + 1);
            const std::string l2 = "P2 " + tag;
            checkEmergenceShapeT013(l2.c_str(), p2.g, dipEnd, N);
            const double e2 = checkEmergenceEnergyT013(l2.c_str(), p2.L, lockP2.L, dipEnd, N);
            std::printf("SC-005 %s: window energy re LOCK P1 %+.2f dB, P2 %+.2f dB (orientation, sine P1: E1 -1.63 / "
                        "-1.88 / -3.68 / -4.41, C3 -1.73 / -5.87 / -4.50 / -4.30 dB at 1 / 5 / 20 / 100 ms)\n",
                        tag.c_str(), e1, e2);

            // P3: noteOn(f, false) mid-emergence under both policies: g bit-identical, no restart.
            const std::size_t mid = std::max<std::size_t>(1, N / 2);
            const std::size_t total = N + 2400;
            const EnvRunT013 plain = runEnvT013(ctlG, f0, {}, total);
            for (const ARP pol : {ARP::Reset, ARP::FreeRunning}) {
                const EnvRunT013 p3 = runEnvT013(ctlG, f0, {armEventT013(mid, pol, false)}, total);
                INFO("P3 policy " << (pol == ARP::Reset ? "Reset" : "FreeRunning"));
                CHECK(std::memcmp(p3.r.g.data(), plain.r.g.data(), total * sizeof(double)) == 0);
            }
        }
    }
}

// =============================================================================
// SC-006(a): every g trajectory is C¹ (plan S1 C-3 bridge rule; E-6, E-8, E-13)
// =============================================================================

TEST_CASE("AnchorOscillator_EnvelopesAreC1", "[systems][profundum][anchor]") {
    const float fC2 = PT::anchorMidiHz(36);
    const double B = boostT013(12.0f);
    REQUIRE(std::abs(B - 3.981072) <= 1e-6);
    const std::vector<EnvCaseT013> cases = envFixtureT013();
    REQUIRE(cases.size() > 50);
    for (const EnvCaseT013& c : cases) {
        INFO(c.name);
        const EnvRunT013 run = runEnvT013(c.initial, fC2, c.events, c.total);

        // The rendered g is the expected program (closed forms in double).
        double worstModel = 0.0;
        for (std::size_t i = 0; i < c.total; ++i)
            worstModel = std::max(worstModel, std::abs(run.r.g[i] - run.modelG[i]));

        const std::vector<PT::AnchorSegmentSpec> specs = specsT013(run.model, c.total);
        const PT::C1Report rep = PT::anchorCheckC1(run.r.g, specs, 0.0, B);
        std::printf("SC-006(a) %s: N_b %s; worst |g - program| %.2e; worstRatio %.4f; boundary jump %.3e; "
                    "inRange %d; pass %d\n",
                    c.name.c_str(), bridgeListT013(run.model).c_str(), worstModel, rep.worstRatio,
                    rep.worstBoundaryJump, rep.inRange ? 1 : 0, rep.pass ? 1 : 0);
        CHECK(worstModel <= 1e-9);
        CHECK(rep.inRange);
        CHECK(rep.pass);

        if (c.expectUnchanged) {   // FR-048: rearm = false leaves g bit-identical
            const EnvRunT013 plain = runEnvT013(c.initial, fC2, {}, c.total);
            CHECK(std::memcmp(run.r.g.data(), plain.r.g.data(), c.total * sizeof(double)) == 0);
        }
        if (c.initial.level == 0.0f) {   // E-13: level 0 with PUNCH is exactly 0
            CHECK(std::all_of(run.r.L.begin(), run.r.L.end(), [](float y) { return y == 0.0f; }));
            CHECK(*std::max_element(run.r.g.begin(), run.r.g.end()) > 1.0);   // the envelope itself still ran
        }
    }
}

// =============================================================================
// SC-006(b)(c): click-free transitions and the sample-delta bound
// =============================================================================

namespace {

/// One SC-006(b)/(c) render: the whole render y (clicks) and its transition window [from, to)
/// with the documented analytic bound terms.
struct TransitionT013 {
    std::string name;
    std::vector<float> y;
    double fs;
    std::size_t from, to;
    double gMax, sMax, dMax, pMax;
};

struct SteadyT013 {
    double dMax, pMax;
};

/// D_max / P_max: a 1 s steady unit-gain render (level 1, LOCK, g = 1, a1 = kMaxCoreH1Amplitude).
SteadyT013 steadyT013(AW w, float f0, double fs) {
    AnchorOscillator a;
    a.prepare(fs);
    a.setControls(envControlsT013(AM::Lock, 0.0f, 40.0f, 15.0f, 1.0f, w));
    a.setRetriggerPhase(ARP::Reset);
    a.noteOn(f0);
    const auto from = static_cast<std::size_t>(std::llround(0.5 * fs));
    const auto n = static_cast<std::size_t>(std::llround(1.5 * fs));
    const PT::AnchorRender r = PT::renderAnchor(a, n, 512);
    return SteadyT013{maxAbsDelta(r.L, from, n), peakAbs(r.L, from, n)};
}

std::size_t countClicksT013(const std::vector<float>& y, double fs) {
    Krate::DSP::TestUtils::ClickDetectorConfig cfg{};   // documented defaults (artifact_detection.h:38-44)
    cfg.sampleRate = static_cast<float>(fs);
    Krate::DSP::TestUtils::ClickDetector detector(cfg);
    detector.prepare();
    return detector.detect(y.data(), y.size()).size();
}

using ActionT013 = std::function<void(AnchorOscillator&)>;

/// A Reset noteOn(f0) with `c`, then `total` samples in `block`-sized blocks, split at each event
/// (applied before its sample). traj, when non-null, holds the whole render's f0PerSample.
std::vector<float> renderActionsT013(double fs, const AnchorOscillator::Controls& c, float f0, std::size_t total,
                                     std::size_t block, const std::vector<std::pair<std::size_t, ActionT013>>& events,
                                     const float* traj = nullptr) {
    AnchorOscillator a;
    a.prepare(fs);
    a.setControls(c);
    a.setRetriggerPhase(ARP::Reset);
    a.noteOn(f0);
    std::vector<float> out;
    out.reserve(total);
    std::size_t pos = 0;
    const auto renderTo = [&](std::size_t to) {
        if (to > pos) {
            const PT::AnchorRender r = PT::renderAnchor(a, to - pos, block, traj != nullptr ? traj + pos : nullptr);
            out.insert(out.end(), r.L.begin(), r.L.end());
            pos = to;
        }
    };
    for (const auto& [at, action] : events) {
        REQUIRE(at >= pos);
        renderTo(at);
        action(a);
    }
    renderTo(total);
    REQUIRE(a.stateFinite());
    return out;
}

/// f0 glides by `semis` over `len` samples from `from` (raised cosine in semitones), then holds.
std::vector<float> glideTrajT013(float f0, double semis, std::size_t total, std::size_t from, std::size_t len) {
    std::vector<float> traj(total, f0);
    for (std::size_t i = from; i < total; ++i) {
        const double x = std::min(1.0, static_cast<double>(i - from) / static_cast<double>(len));
        const double s = semis * 0.5 * (1.0 - std::cos(PT::kAnchorPiD * x));
        traj[i] = static_cast<float>(static_cast<double>(f0) * std::exp2(s / 12.0));
    }
    return traj;
}

/// Every SC-006(b)/(c) transition: the SC-006(a) fixture (minus Reset noteOns on a sounding voice,
/// which snap, FR-006), level and a1 steps, waveform switches between every ordered pair mid-cycle, a
/// LOCK Glide (200 ms, ±7 st), a Drop-shaped trajectory (−12 st, 100 ms) and the E-2 triangle sweep.
std::vector<TransitionT013> buildTransitionsT013() {
    std::vector<TransitionT013> out;
    const float fC2 = PT::anchorMidiHz(36);
    const SteadyT013 sineC2 = steadyT013(AW::Sine, fC2, kFsT013);
    // T014: D_max / P_max of a FOLLOW render are those at its top frequency kEnvFollowRatio·f0.
    const SteadyT013 sineFollowC2 = steadyT013(AW::Sine, kEnvFollowRatio * fC2, kFsT013);
    const double firstStep = 1.0 - std::exp(-1000.0 / kFsT013);   // the level smoother's first step per unit

    // (1) The envelope fixture.
    for (const EnvCaseT013& c : envFixtureT013()) {
        if (c.resetsSounding)
            continue;
        bool follows = c.initial.mode == AM::Follow;
        for (const EnvEventT013& e : c.events)
            follows = follows || (e.setControls && e.controls.mode == AM::Follow);
        const SteadyT013& steady = follows ? sineFollowC2 : sineC2;
        const EnvRunT013 run = runEnvT013(c.initial, fC2, c.events, c.total);
        const std::size_t from = std::max<std::size_t>(c.focus, 1);
        const std::size_t to = std::min(c.total, std::max(programEndT013(run.model, c.total), from) + 1);
        double gPeak = 0.0;
        for (std::size_t i = from - 1; i < to; ++i)
            gPeak = std::max(gPeak, run.modelG[i]);
        double sMax = 0.0;
        for (std::size_t i = 0; i < run.model.segs.size(); ++i) {
            const ModelSegT013& s = run.model.segs[i];
            const std::size_t end = i + 1 < run.model.segs.size() ? run.model.segs[i + 1].start : c.total;
            if (s.start < to && end + 1 >= from)
                sMax = std::max(sMax, modelPeakSlopeT013(s));
        }
        out.push_back(TransitionT013{c.name, run.r.L, kFsT013, from, to,
                                     static_cast<double>(c.initial.level) * gPeak, sMax, steady.dMax, steady.pMax});
    }

    // (1b) E-8: the minimum-time arms at C3, the highest test note (GHOST 1 ms, PUNCH 5 ms at +12 dB).
    {
        const float fC3 = PT::anchorMidiHz(48);
        const SteadyT013 sineC3 = steadyT013(AW::Sine, fC3, kFsT013);
        for (const AM m : {AM::Ghost, AM::Punch}) {
            const AnchorOscillator::Controls c = envControlsT013(m, 12.0f, 5.0f, 1.0f);
            const std::size_t total = 4800;
            const EnvRunT013 run = runEnvT013(c, fC3, {}, total);
            const std::size_t to = programEndT013(run.model, total) + 1;
            double sMax = 0.0;
            for (const ModelSegT013& s : run.model.segs)
                sMax = std::max(sMax, modelPeakSlopeT013(s));
            const double gPeak = *std::max_element(run.modelG.begin(), run.modelG.end());
            out.push_back(TransitionT013{std::string("E-8 C3 ") + modeNameT013(m) + " snap arm (minimum time)", run.r.L,
                                         kFsT013, 1, to, gPeak, sMax, sineC3.dMax, sineC3.pMax});
        }
    }

    // (2) Level steps 0 -> 1 -> 0 and a1 steps 0 -> kMaxCoreH1Amplitude -> 0 (C2 sine, LOCK).
    {
        constexpr std::size_t kTotal = 72000;
        AnchorOscillator::Controls c = envControlsT013(AM::Lock, 0.0f, 40.0f, 15.0f, 0.0f);
        AnchorOscillator::Controls c1 = c;
        c1.level = 1.0f;
        const std::vector<float> yLevel = renderActionsT013(
            kFsT013, c, fC2, kTotal, 64,
            {{24000, [c1](AnchorOscillator& a) { a.setControls(c1); }},
             {48000, [c](AnchorOscillator& a) { a.setControls(c); }}});
        out.push_back(TransitionT013{"level 0 -> 1 -> 0", yLevel, kFsT013, 24000, kTotal, 1.0, firstStep,
                                     sineC2.dMax, sineC2.pMax});

        const float a1Max = AnchorOscillator::kMaxCoreH1Amplitude;
        std::vector<std::pair<std::size_t, ActionT013>> a1Events{
            {24000, [a1Max](AnchorOscillator& a) { a.setCoreH1Amplitude(a1Max); }},
            {48000, [](AnchorOscillator& a) { a.setCoreH1Amplitude(0.0f); }}};
        const std::vector<float> yA1 = [&] {   // a1 starts at 0: set before the noteOn (snapped by it)
            AnchorOscillator a;
            a.prepare(kFsT013);
            a.setControls(c1);
            a.setCoreH1Amplitude(0.0f);
            a.setRetriggerPhase(ARP::Reset);
            a.noteOn(fC2);
            std::vector<float> y;
            std::size_t pos = 0;
            for (const auto& [at, action] : a1Events) {
                const PT::AnchorRender r = PT::renderAnchor(a, at - pos, 64);
                y.insert(y.end(), r.L.begin(), r.L.end());
                pos = at;
                action(a);
            }
            const PT::AnchorRender r = PT::renderAnchor(a, kTotal - pos, 64);
            y.insert(y.end(), r.L.begin(), r.L.end());
            REQUIRE(a.stateFinite());
            return y;
        }();
        // S_max normalised to a1 = kMaxCoreH1Amplitude (P_max carries a1max).
        out.push_back(TransitionT013{"a1 0 -> max -> 0", yA1, kFsT013, 24000, kTotal, 1.0, firstStep, sineC2.dMax,
                                     sineC2.pMax});
    }

    // (3) Waveform switches between every ordered pair, mid-cycle (C2, LOCK, level 1).
    {
        const std::size_t nx = samplesT013(AnchorOscillator::kWaveformCrossfadeMs);
        const std::size_t sw = 4800 + 183;   // about a quarter period of C2 past a block boundary
        const std::size_t total = sw + nx + 4800;
        const AnchorOscillator::Controls base = envControlsT013(AM::Lock, 0.0f, 40.0f, 15.0f);
        std::array<std::vector<float>, kAllWavesT011.size()> ref;
        for (std::size_t i = 0; i < kAllWavesT011.size(); ++i) {
            AnchorOscillator::Controls c = base;
            c.waveform = kAllWavesT011[i];
            ref[i] = renderActionsT013(kFsT013, c, fC2, total, 512, {});
        }
        const double dcMax = PT::kAnchorPiD / (2.0 * static_cast<double>(nx));
        for (const AW from : kAllWavesT011) {
            for (const AW to : kAllWavesT011) {
                if (from == to)
                    continue;
                AnchorOscillator::Controls c0 = base;
                c0.waveform = from;
                AnchorOscillator::Controls c1 = base;
                c1.waveform = to;
                const std::vector<float> y = renderActionsT013(
                    kFsT013, c0, fC2, total, 512, {{sw, [c1](AnchorOscillator& a) { a.setControls(c1); }}});
                const std::vector<float>& rOld = ref[static_cast<std::size_t>(from)];
                const std::vector<float>& rNew = ref[static_cast<std::size_t>(to)];
                double diff = 0.0;
                for (std::size_t i = 2400; i < total; ++i)
                    diff = std::max(diff, std::abs(static_cast<double>(rNew[i]) - static_cast<double>(rOld[i])));
                const double dMax = std::max(maxAbsDelta(rOld, 2400, total), maxAbsDelta(rNew, 2400, total));
                // S·P term = Δc_max·max|w_new − w_old| (SC-006(c)): passed as S = Δc_max, P = that max.
                out.push_back(TransitionT013{std::string("waveform ") + std::to_string(static_cast<int>(from)) + " -> "
                                                 + std::to_string(static_cast<int>(to)),
                                             y, kFsT013, sw, sw + nx + 1, 1.0, dcMax, dMax, diff});
            }
        }
    }

    // (4) LOCK Glide (200 ms, ±7 st) and a Drop-shaped trajectory (−12 st, 100 ms), C2 sine.
    {
        const AnchorOscillator::Controls c = envControlsT013(AM::Lock, 0.0f, 40.0f, 15.0f);
        constexpr std::size_t kFrom = 9600;
        constexpr std::size_t kTotal = 33600;
        const auto glideLen = static_cast<std::size_t>(std::llround(0.2 * kFsT013));
        for (const double semis : {7.0, -7.0}) {
            const std::vector<float> traj = glideTrajT013(fC2, semis, kTotal, kFrom, glideLen);
            const std::vector<float> y = renderActionsT013(kFsT013, c, fC2, kTotal, 64, {}, traj.data());
            const float fTop = std::max(traj.front(), traj.back());
            const SteadyT013 top = steadyT013(AW::Sine, fTop, kFsT013);
            out.push_back(TransitionT013{semis > 0.0 ? "LOCK glide +7 st" : "LOCK glide -7 st", y, kFsT013, kFrom,
                                         kFrom + glideLen + 32, 1.0, 0.0, top.dMax, top.pMax});
        }
        const auto dropLen = static_cast<std::size_t>(std::llround(0.1 * kFsT013));
        const std::vector<float> traj = dropTrajectory(fC2, kTotal, kFrom, dropLen);
        const std::vector<float> y = renderActionsT013(kFsT013, c, fC2, kTotal, 64, {}, traj.data());
        out.push_back(TransitionT013{"Drop -12 st 100 ms", y, kFsT013, kFrom, kFrom + dropLen + 32, 1.0, 0.0,
                                     sineC2.dMax, sineC2.pMax});
    }

    // (4b) T014: a FOLLOW Drop-shaped trajectory (r = kEnvFollowRatio), and a LOCK <-> FOLLOW
    // trajectory-input switch: LOCK on the resolved (constant) pitch, then FOLLOW fed the Drop, then
    // LOCK again after the Drop has returned (FR-049). C2 sine, level 1, host block 64.
    {
        const AnchorOscillator::Controls lock = envControlsT013(AM::Lock, 0.0f, 40.0f, 15.0f);
        const AnchorOscillator::Controls follow = envControlsT013(AM::Follow, 0.0f, 40.0f, 15.0f);
        const auto dropLen = static_cast<std::size_t>(std::llround(0.1 * kFsT013));
        const ActionT013 setRatio = [](AnchorOscillator& a) { a.setFollowRatio(kEnvFollowRatio); };

        constexpr std::size_t kDropFrom = 9600;
        constexpr std::size_t kTotal = 33600;
        const std::vector<float> drop = dropTrajectory(fC2, kTotal, kDropFrom, dropLen);
        const std::vector<float> yFollow =
            renderActionsT013(kFsT013, follow, fC2, kTotal, 64, {{0, setRatio}}, drop.data());
        out.push_back(TransitionT013{"FOLLOW Drop -12 st 100 ms (r 1.5)", yFollow, kFsT013, kDropFrom,
                                     kDropFrom + dropLen + 32, 1.0, 0.0, sineFollowC2.dMax, sineFollowC2.pMax});

        constexpr std::size_t kToFollow = 9600 + 183;
        constexpr std::size_t kSwitchDropFrom = 14400;
        constexpr std::size_t kToLock = 28800 + 183;
        constexpr std::size_t kSwitchTotal = 43200;
        const std::vector<float> traj = dropTrajectory(fC2, kSwitchTotal, kSwitchDropFrom, dropLen);
        REQUIRE(kSwitchDropFrom + dropLen < kToLock);   // LOCK sees only the resolved (constant) pitch
        const std::vector<float> ySwitch = renderActionsT013(
            kFsT013, lock, fC2, kSwitchTotal, 64,
            {{0, setRatio},
             {kToFollow, [follow](AnchorOscillator& a) { a.setControls(follow); }},
             {kToLock, [lock](AnchorOscillator& a) { a.setControls(lock); }}},
            traj.data());
        out.push_back(TransitionT013{"LOCK -> FOLLOW (r 1.5, Drop) -> LOCK", ySwitch, kFsT013, kToFollow,
                                     kToLock + 64, 1.0, 0.0, sineFollowC2.dMax, sineFollowC2.pMax});
    }

    // (5) E-2: a slow upward f0 sweep of the triangle from 2 kHz to capHz over 10 s at 44.1 kHz.
    {
        constexpr double kFs = 44100.0;
        const float capHz = SpectralShapeRecipe::capFrequency(kFs);
        const auto n = static_cast<std::size_t>(std::llround(10.0 * kFs));
        std::vector<float> traj(n);
        for (std::size_t i = 0; i < n; ++i) {
            const double x = static_cast<double>(i) / static_cast<double>(n - 1);
            traj[i] = static_cast<float>(2000.0 * std::pow(static_cast<double>(capHz) / 2000.0, x));
        }
        traj.back() = capHz;
        const AnchorOscillator::Controls c = envControlsT013(AM::Lock, 0.0f, 40.0f, 15.0f, 1.0f, AW::Triangle);
        const std::vector<float> y = renderActionsT013(kFs, c, 2000.0f, n, 512, {}, traj.data());
        const SteadyT013 top = steadyT013(AW::Triangle, capHz, kFs);
        out.push_back(TransitionT013{"E-2 triangle sweep 2 kHz -> capHz, 44.1 kHz", y, kFs, 1, n, 1.0, 0.0, top.dMax,
                                     top.pMax});
    }
    return out;
}

}  // namespace

TEST_CASE("AnchorOscillator_TransitionsClickFree", "[systems][profundum][anchor]") {   // SC-006(b)
    const std::vector<TransitionT013> all = buildTransitionsT013();
    REQUIRE(all.size() > 70);
    std::size_t total = 0;
    for (const TransitionT013& t : all) {
        const std::size_t clicks = countClicksT013(t.y, t.fs);
        total += clicks;
        if (clicks != 0)
            std::printf("SC-006(b) %s: %zu click(s)\n", t.name.c_str(), clicks);
        INFO(t.name);
        CHECK(clicks == 0);
    }
    std::printf("SC-006(b): %zu renders, %zu clicks in total (bar 0)\n", all.size(), total);
}

TEST_CASE("AnchorOscillator_ClickDetectorPositiveControl", "[systems][profundum][anchor]") {   // SC-006(b)
    // A spliced instantaneous level step 0 -> 1 in a steady sine render (C2, 48 kHz, level 1).
    const AnchorOscillator::Controls c = envControlsT013(AM::Lock, 0.0f, 40.0f, 15.0f);
    std::vector<float> y = renderActionsT013(kFsT013, c, PT::anchorMidiHz(36), 96000, 512, {});
    std::fill(y.begin(), y.begin() + 48000 + 183, 0.0f);
    const std::size_t clicks = countClicksT013(y, kFsT013);
    std::printf("SC-006(b) positive control (spliced level step 0 -> 1): %zu click(s) (bar >= 1)\n", clicks);
    CHECK(clicks >= 1);
}

TEST_CASE("AnchorOscillator_TransitionDeltaBound", "[systems][profundum][anchor]") {   // SC-006(c)
    const std::vector<TransitionT013> all = buildTransitionsT013();
    REQUIRE(all.size() > 70);
    double worstRatio = 0.0;
    for (const TransitionT013& t : all) {
        REQUIRE(t.from >= 1);
        REQUIRE(t.to <= t.y.size());
        const double worst = maxAbsDelta(t.y, t.from, t.to);
        const double bound = 1.1 * (t.gMax * t.dMax + t.sMax * t.pMax);
        REQUIRE(bound > 0.0);
        worstRatio = std::max(worstRatio, worst / bound);
        std::printf("SC-006(c) %s: max|dy| %.6e, bound %.6e (G %.4f D %.6e S %.6e P %.6f), ratio %.4f\n",
                    t.name.c_str(), worst, bound, t.gMax, t.dMax, t.sMax, t.pMax, worst / bound);
        INFO(t.name);
        CHECK(worst <= bound);
    }
    std::printf("SC-006(c): worst ratio %.4f over %zu transitions (bar 1)\n", worstRatio, all.size());
}

// =============================================================================
// SC-027: the re-arm event (FR-048; plan S1 C-8)
// =============================================================================

TEST_CASE("AnchorOscillator_RearmEvent", "[systems][profundum][anchor]") {
    const float fC2 = PT::anchorMidiHz(36);
    const std::size_t nr = samplesT013(AnchorOscillator::kPunchRiseMs);
    const std::size_t nd = samplesT013(AnchorOscillator::kGhostDipMs);
    const AnchorOscillator::Controls punch = envControlsT013(AM::Punch, 12.0f, 40.0f, 15.0f);
    const std::size_t nt = samplesT013(punch.punchTimeMs);
    const double B = boostT013(punch.punchBoostDb);

    SECTION("(a) timing: offset 0 of the block after the call, independent of the host blocks") {
        constexpr std::size_t kTotal = 24000;
        struct BlockRender {
            std::vector<float> y;
            std::vector<double> gEnd;   // g at each block's last sample
            std::vector<std::size_t> endAt;
        };
        // PUNCH snap arm at 0 (settled by 2000 samples); a FreeRunning re-arm before the block at armAt.
        const auto renderBlocks = [&](std::size_t block, std::size_t armAt) {
            REQUIRE(armAt % block == 0);
            AnchorOscillator a;
            a.prepare(kFsT013);
            a.setControls(punch);
            a.setRetriggerPhase(ARP::Reset);
            a.noteOn(fC2);
            BlockRender br;
            br.y.assign(kTotal, 0.0f);
            std::vector<float> right(kTotal, 0.0f);
            for (std::size_t pos = 0; pos < kTotal; pos += block) {
                const std::size_t len = std::min(block, kTotal - pos);
                if (pos == armAt) {
                    a.setRetriggerPhase(ARP::FreeRunning);
                    a.noteOn(fC2, true);
                }
                a.processBlock(br.y.data() + pos, right.data() + pos, len);
                br.gEnd.push_back(a.modeGain());
                br.endAt.push_back(pos + len - 1);
            }
            PT::requireLREqual(br.y.data(), right.data(), kTotal);
            return br;
        };
        const auto perSample = [&](std::size_t armAt) {
            return runEnvT013(punch, fC2, {armEventT013(armAt, ARP::FreeRunning, true)}, kTotal);
        };

        // A sample that is a block boundary for 1, 37 and 512: every partition is bit-identical.
        constexpr std::size_t kCommon = 37 * 512;
        const EnvRunT013 ref = perSample(kCommon);
        for (const std::size_t block : {std::size_t{1}, std::size_t{37}, std::size_t{512}}) {
            INFO("block " << block << " (common boundary " << kCommon << ")");
            const BlockRender br = renderBlocks(block, kCommon);
            // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) - intentional bit-exact check
            CHECK(std::memcmp(br.y.data(), ref.r.L.data(), kTotal * sizeof(float)) == 0);
            bool gEqual = true;
            for (std::size_t k = 0; k < br.endAt.size(); ++k)
                gEqual = gEqual && sameBitsT013(br.gEnd[k], ref.r.g[br.endAt[k]]);
            CHECK(gEqual);
        }

        // The setter before the block containing sample 20000: the response begins at that block's
        // offset 0, and the g trace from there is the same for every partition.
        constexpr std::size_t kTarget = 20000;
        constexpr std::size_t kCompare = 4000;
        std::vector<double> firstTrace;
        for (const std::size_t block : {std::size_t{1}, std::size_t{37}, std::size_t{512}}) {
            const std::size_t armAt = kTarget / block * block;
            INFO("block " << block << " arm at " << armAt);
            const BlockRender br = renderBlocks(block, armAt);
            const EnvRunT013 r1 = perSample(armAt);
            // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) - intentional bit-exact check
            CHECK(std::memcmp(br.y.data(), r1.r.L.data(), kTotal * sizeof(float)) == 0);
            bool before = true;
            for (std::size_t i = nr + nt; i <= armAt; ++i)
                before = before && r1.r.g[i] == 1.0;
            CHECK(before);                         // untouched up to and including the bridge's start value
            CHECK(r1.r.g[armAt + 1] > 1.0);        // and moving from that block's offset 0
            const std::vector<double> trace(r1.r.g.begin() + static_cast<std::ptrdiff_t>(armAt),
                                            r1.r.g.begin() + static_cast<std::ptrdiff_t>(armAt + kCompare));
            if (firstTrace.empty())
                firstTrace = trace;
            // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) - intentional bit-exact check
            CHECK(std::memcmp(trace.data(), firstTrace.data(), kCompare * sizeof(double)) == 0);
        }
    }

    SECTION("(b) PUNCH bridged arm: peak B and the SC-004(b) return measured from the arm") {
        struct ArmAt {
            const char* name;
            std::size_t at;
        };
        for (const ArmAt p : {ArmAt{"steady", nr + nt + 500}, ArmAt{"mid-fall", nr + nt / 2}, ArmAt{"mid-rise", nr / 2}}) {
            const std::size_t total = p.at + nr + nt + 2400;
            const EnvRunT013 run = runEnvT013(punch, fC2, {armEventT013(p.at, ARP::FreeRunning, true)}, total);
            REQUIRE(run.model.bridgeLens.size() == 1);
            const std::size_t nb = run.model.bridgeLens[0];
            const auto peakIt = std::max_element(run.r.g.begin() + static_cast<std::ptrdiff_t>(p.at), run.r.g.end());
            const auto peakIdx = static_cast<std::size_t>(peakIt - run.r.g.begin());
            std::size_t ret = total;
            for (std::size_t i = peakIdx + 1; i < total; ++i) {
                if (sameBitsT013(run.r.g[i], 1.0)) {
                    ret = i;
                    break;
                }
            }
            REQUIRE(ret < total);
            double worstAfter = 0.0;
            for (std::size_t i = ret; i < total; ++i)
                worstAfter = std::max(worstAfter, std::abs(dbT013(run.r.g[i])));
            const std::size_t expected = p.at + nb + nt;
            std::printf("SC-027(b) PUNCH bridged arm at %s: N_b %zu (nominal %zu), peak %+.5f dB, return at +%zu "
                        "(expected +%zu)\n",
                        p.name, nb, nr, dbT013(*peakIt / B), ret - p.at, nb + nt);
            INFO(p.name);
            CHECK(std::abs(dbT013(*peakIt / B)) <= 0.1);
            CHECK(std::abs(static_cast<long long>(ret) - static_cast<long long>(expected)) <= 1);
            CHECK(run.r.g[ret - 1] > 1.0);
            CHECK(worstAfter <= 0.01);
        }
    }

    SECTION("(c) GHOST bridged arm: the dip reaches 0 within kGhostDipMs + 1, then SC-005 (a)-(c)") {
        const AnchorOscillator::Controls ghost = envControlsT013(AM::Ghost, 12.0f, 40.0f, 15.0f);
        const std::size_t ng = samplesT013(ghost.ghostTimeMs);
        struct ArmAt {
            const char* name;
            std::size_t at;
        };
        for (const ArmAt p : {ArmAt{"steady", ng + 500}, ArmAt{"mid-rise", ng / 2}}) {
            const std::size_t total = p.at + nd + ng + 2400;
            const EnvRunT013 run = runEnvT013(ghost, fC2, {armEventT013(p.at, ARP::FreeRunning, true)}, total);
            std::size_t zeroAt = total;
            for (std::size_t i = p.at; i < total; ++i) {
                if (run.r.g[i] == 0.0) {
                    zeroAt = i;
                    break;
                }
            }
            std::printf("SC-027(c) GHOST bridged arm at %s: N_b %s, g == 0 at +%zu (bar %zu)\n", p.name,
                        bridgeListT013(run.model).c_str(), zeroAt - p.at, nd + 1);
            INFO(p.name);
            REQUIRE(zeroAt - p.at <= nd + 1);
            const std::string label = std::string("SC-027(c) ") + p.name;
            checkEmergenceShapeT013(label.c_str(), run.r.g, zeroAt, ng);
        }
    }

    SECTION("(d) rearm = false: g untouched under both policies and every mode; phase independent of rearm") {
        // T014: runEnvT013 holds r = kEnvFollowRatio, so the Follow row runs FOLLOW as itself.
        for (const AM m : {AM::Lock, AM::Follow, AM::Punch, AM::Ghost}) {
            const AnchorOscillator::Controls c = envControlsT013(m, 12.0f, 40.0f, 15.0f);
            const std::size_t ng = samplesT013(c.ghostTimeMs);
            std::size_t at = 1000;
            if (m == AM::Punch)
                at = nr + nt / 2;
            else if (m == AM::Ghost)
                at = ng / 2;
            const std::size_t total = at + 4000;
            const EnvRunT013 plain = runEnvT013(c, fC2, {}, total);
            for (const ARP pol : {ARP::Reset, ARP::FreeRunning}) {
                const EnvRunT013 noArm = runEnvT013(c, fC2, {armEventT013(at, pol, false)}, total);
                const EnvRunT013 arm = runEnvT013(c, fC2, {armEventT013(at, pol, true)}, total);
                INFO("mode " << modeNameT013(m) << " policy " << (pol == ARP::Reset ? "Reset" : "FreeRunning"));
                CHECK(std::memcmp(noArm.r.g.data(), plain.r.g.data(), total * sizeof(double)) == 0);
                CHECK(std::memcmp(noArm.r.phase.data(), arm.r.phase.data(), total * sizeof(double)) == 0);
            }
        }
    }

    SECTION("(e) a noteOn from not sounding always arms") {
        for (const ARP pol : {ARP::Reset, ARP::FreeRunning}) {
            const auto render = [&](bool rearm) {
                AnchorOscillator a;
                a.prepare(kFsT013);
                a.setControls(punch);
                a.setRetriggerPhase(pol);
                a.noteOn(fC2, rearm);
                return PT::renderAnchor(a, nr + nt + 480, 1, nullptr, nullptr, true);
            };
            const PT::AnchorRender withArm = render(true);
            const PT::AnchorRender noArm = render(false);
            INFO("policy " << (pol == ARP::Reset ? "Reset" : "FreeRunning"));
            CHECK(std::memcmp(noArm.g.data(), withArm.g.data(), withArm.g.size() * sizeof(double)) == 0);
            CHECK(std::memcmp(noArm.L.data(), withArm.L.data(), withArm.L.size() * sizeof(float)) == 0);
            CHECK(*std::max_element(noArm.g.begin(), noArm.g.end()) > 1.0);
        }
    }
}

// =============================================================================
// SC-017: retrigger policies — a Reset noteOn with a re-arm snaps every stateful control
// =============================================================================

TEST_CASE("AnchorOscillator_RetriggerPolicies", "[systems][profundum][anchor]") {
    const float fC2 = PT::anchorMidiHz(36);
    const float fE1 = PT::anchorMidiHz(28);
    constexpr std::size_t kPost = 9600;

    SECTION("renders after a Reset noteOn(f, true) are independent of the prior state, every mode") {
        for (const AM m : {AM::Lock, AM::Follow, AM::Punch, AM::Ghost}) {
            const AnchorOscillator::Controls target = envControlsT013(m, 9.0f, 30.0f, 10.0f, 0.8f, AW::RoundedSine);
            const auto after = [&](const ActionT013& prior) {
                AnchorOscillator a;
                a.prepare(kFsT013);
                prior(a);
                a.setControls(target);
                a.setRetriggerPhase(ARP::Reset);
                a.noteOn(fC2, true);
                return PT::renderAnchor(a, kPost, 1, nullptr, nullptr, true);
            };
            const auto start = [](AnchorOscillator& a, const AnchorOscillator::Controls& c, float f) {
                a.setControls(c);
                a.setRetriggerPhase(ARP::Reset);
                a.noteOn(f);
            };
            const PT::AnchorRender ref = after([](AnchorOscillator&) {});
            REQUIRE(ref.L[0] == 0.0f);

            const std::vector<std::pair<const char*, ActionT013>> priors{
                {"other f0 and Controls",
                 [&](AnchorOscillator& a) {
                     start(a, envControlsT013(AM::Ghost, 3.0f, 200.0f, 80.0f, 0.3f, AW::Triangle), fE1);
                     (void)PT::renderAnchor(a, 14400, 512);
                 }},
                {"running waveform crossfade",
                 [&](AnchorOscillator& a) {
                     start(a, envControlsT013(m, 9.0f, 30.0f, 10.0f, 0.8f, AW::Sine), fC2);
                     (void)PT::renderAnchor(a, 14400, 512);
                     a.setControls(envControlsT013(m, 9.0f, 30.0f, 10.0f, 0.8f, AW::Triangle));
                     (void)PT::renderAnchor(a, 100, 512);
                 }},
                {"moving level smoother",
                 [&](AnchorOscillator& a) {
                     start(a, envControlsT013(m, 9.0f, 30.0f, 10.0f, 0.0f, AW::RoundedSine), fC2);
                     (void)PT::renderAnchor(a, 14400, 512);
                     a.setControls(envControlsT013(m, 9.0f, 30.0f, 10.0f, 1.0f, AW::RoundedSine));
                     (void)PT::renderAnchor(a, 50, 512);
                 }},
                {"g mid-PUNCH",
                 [&](AnchorOscillator& a) {
                     start(a, envControlsT013(AM::Punch, 12.0f, 250.0f, 10.0f, 0.8f, AW::RoundedSine), fC2);
                     (void)PT::renderAnchor(a, 4800, 512);
                 }},
                {"g mid-GHOST",
                 [&](AnchorOscillator& a) {
                     start(a, envControlsT013(AM::Ghost, 9.0f, 30.0f, 100.0f, 0.8f, AW::RoundedSine), fC2);
                     (void)PT::renderAnchor(a, 2400, 512);
                 }},
                {"0.3 s elapsed",
                 [&](AnchorOscillator& a) {
                     start(a, target, fC2);
                     (void)PT::renderAnchor(a, 14400, 512);
                 }},
                {"2 s elapsed",
                 [&](AnchorOscillator& a) {
                     start(a, target, fC2);
                     (void)PT::renderAnchor(a, 96000, 512);
                 }},
            };
            for (const auto& [name, prior] : priors) {
                const PT::AnchorRender r = after(prior);
                INFO("mode " << modeNameT013(m) << ", prior state: " << name);
                // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) - intentional bit-exact check
                CHECK(std::memcmp(r.L.data(), ref.L.data(), kPost * sizeof(float)) == 0);
                // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) - intentional bit-exact check
                CHECK(std::memcmp(r.g.data(), ref.g.data(), kPost * sizeof(double)) == 0);
            }
        }
    }

    SECTION("rearm = false: g untouched under both policies; under Reset the phase restart is the only "
            "discontinuity") {
        const std::size_t nr = samplesT013(AnchorOscillator::kPunchRiseMs);
        for (const AM m : {AM::Punch, AM::Ghost}) {
            const AnchorOscillator::Controls c = envControlsT013(m, 12.0f, 250.0f, 100.0f);
            const std::size_t at = m == AM::Punch ? nr + samplesT013(c.punchTimeMs) / 2 : samplesT013(c.ghostTimeMs) / 2;
            const std::size_t total = at + 14400;
            const EnvRunT013 plain = runEnvT013(c, fC2, {}, total);
            for (const ARP pol : {ARP::Reset, ARP::FreeRunning}) {
                const EnvRunT013 r = runEnvT013(c, fC2, {armEventT013(at, pol, false)}, total);
                INFO("mode " << modeNameT013(m) << " policy " << (pol == ARP::Reset ? "Reset" : "FreeRunning"));
                CHECK(std::memcmp(r.r.g.data(), plain.r.g.data(), total * sizeof(double)) == 0);
                CHECK(std::memcmp(r.r.L.data(), plain.r.L.data(), at * sizeof(float)) == 0);
                if (pol == ARP::FreeRunning) {   // same f0: the phase continues bit for bit
                    CHECK(std::memcmp(r.r.phase.data(), plain.r.phase.data(), total * sizeof(double)) == 0);
                    CHECK(std::memcmp(r.r.L.data(), plain.r.L.data(), total * sizeof(float)) == 0);
                    continue;
                }
                CHECK(sameBitsT013(r.r.phase[at], r.r.inc[at]));   // φ restarted at 0 at the noteOn
                double worst = 0.0;
                for (std::size_t n = 1; n < total; ++n) {
                    if (n == at)
                        continue;
                    double d = r.r.phase[n] - r.r.phase[n - 1];
                    d -= std::floor(d + 0.5);
                    worst = std::max(worst, std::abs(d - r.r.inc[n]));
                }
                std::printf("SC-017 rearm=false Reset (%s): worst phase-step error away from the noteOn %.3e cycles\n",
                            modeNameT013(m), worst);
                CHECK(worst <= 1e-12);
            }
        }
    }
}

// =============================================================================
// T014: FOLLOW (plan S4.10; FR-045, FR-047, FR-049) — SC-008(b)(c), SC-025
// =============================================================================

namespace {

constexpr double kFsT014 = 48000.0;

double centsT014(double f, double ref) {
    return 1200.0 * std::log2(f / ref);
}

/// f_inst[n] = wrap(phase[n] − phase[n−1])·fs for n ≥ 1 (f[0] = 0): the increment applied after
/// sample n (SC-008(b)).
std::vector<double> instFreqT014(const std::vector<double>& phase, double fs) {
    std::vector<double> f(phase.size(), 0.0);
    for (std::size_t n = 1; n < phase.size(); ++n)
        f[n] = PT::anchorInstFreqHz(phase, n, fs);
    return f;
}

/// A fresh sine Anchor in `mode` (level 1) with held ratio r0 and a Reset noteOn(f0), `total`
/// samples in 1-sample blocks; `step` runs before sample `at`. f0Traj / rTraj (whole render) are
/// passed when non-null.
std::vector<double> renderStepT014(AM mode, float f0, float r0, std::size_t at, std::size_t total,
                                   const ActionT013& step, const float* f0Traj = nullptr,
                                   const float* rTraj = nullptr) {
    REQUIRE(at > 0);
    REQUIRE(at < total);
    AnchorOscillator a;
    a.prepare(kFsT014);
    a.setControls(envControlsT013(mode, 6.0f, 40.0f, 15.0f));
    a.setFollowRatio(r0);
    a.setRetriggerPhase(ARP::Reset);
    a.noteOn(f0);
    PT::AnchorRender r = PT::renderAnchor(a, at, 1, f0Traj, rTraj, true);
    step(a);
    appendT013(r, PT::renderAnchor(a, total - at, 1, f0Traj != nullptr ? f0Traj + at : nullptr,
                                   rTraj != nullptr ? rTraj + at : nullptr, true));
    REQUIRE(a.stateFinite());
    REQUIRE(r.phase.size() == total);
    return instFreqT014(r.phase, kFsT014);
}

struct StepReportT014 {
    std::size_t latency;   // first n − at with |cents(f[n], newHz)| ≤ tol
    bool stays;            // every later sample within tol
    bool bounded;          // every f[n ≥ at] within [min, max] of the old and new frequency
};

/// SC-008(b) / SC-025(1) step analysis. The old frequency is the measured f[at − 1] (checked against
/// oldHz), the new one the measured last sample (checked against newHz); the interval allows a
/// 1e-6 relative slack for the mirror's float rounding (C-5).
StepReportT014 analyseStepT014(const std::vector<double>& f, std::size_t at, double oldHz, double newHz,
                               double tolCents) {
    const double fOld = f[at - 1];
    const double fNew = f.back();
    REQUIRE(std::abs(centsT014(fOld, oldHz)) <= 0.01);
    REQUIRE(std::abs(centsT014(fNew, newHz)) <= 0.01);
    StepReportT014 rep{f.size(), true, true};
    for (std::size_t n = at; n < f.size(); ++n) {
        const bool within = std::abs(centsT014(f[n], newHz)) <= tolCents;
        if (rep.latency == f.size()) {
            if (within)
                rep.latency = n - at;
        } else if (!within) {
            rep.stays = false;
        }
    }
    const double lo = std::min(fOld, fNew) * (1.0 - 1e-6);
    const double hi = std::max(fOld, fNew) * (1.0 + 1e-6);
    for (std::size_t n = at; n < f.size(); ++n)
        rep.bounded = rep.bounded && f[n] >= lo && f[n] <= hi;
    return rep;
}

/// A fresh Anchor in `mode` (level 1, waveform w, held ratio r, Reset noteOn f0), `total` samples in
/// `block`-sized blocks with the optional whole-render trajectories; returns L.
std::vector<float> renderModeT014(AM mode, AW w, float f0, float r, std::size_t total, std::size_t block,
                                  const float* f0Traj = nullptr, const float* rTraj = nullptr) {
    AnchorOscillator a;
    a.prepare(kFsT014);
    a.setControls(envControlsT013(mode, 6.0f, 40.0f, 15.0f, 1.0f, w));
    a.setFollowRatio(r);
    a.setRetriggerPhase(ARP::Reset);
    a.noteOn(f0);
    PT::AnchorRender out = PT::renderAnchor(a, total, block, f0Traj, rTraj);
    REQUIRE(a.stateFinite());
    return std::move(out.L);
}

/// The SC-003(b) trajectory shape over a short render: a 5 Hz ±1-semitone vibrato plus 200 ms
/// raised-cosine bends to +12 semitones at 1 s and to −12 semitones at 2 s.
std::vector<float> vibratoBendT014(float f0, double fs, std::size_t total) {
    std::vector<float> traj(total, f0);
    for (std::size_t i = 0; i < total; ++i) {
        const double t = static_cast<double>(i) / fs;
        double semis = std::sin(PT::kTwoPi * 5.0 * t);
        for (const double tb : {1.0, 2.0}) {
            if (t >= tb && t < tb + 0.2) {
                const double sign = tb < 1.5 ? 1.0 : -1.0;
                semis += sign * 12.0 * 0.5 * (1.0 - std::cos(PT::kTwoPi * (t - tb) / 0.2));
            }
        }
        traj[i] = static_cast<float>(static_cast<double>(f0) * std::exp2(semis / 12.0));
    }
    return traj;
}

}  // namespace

TEST_CASE("AnchorOscillator_FollowLatency", "[systems][profundum][anchor]") {   // SC-008(b)
    const float fC2 = PT::anchorMidiHz(36);
    const float fG2 = PT::anchorMidiHz(43);   // a fifth up
    constexpr float kR = 1.5f;
    constexpr std::size_t kBase = 4800;   // a grid sample (300·16); the step lands at kBase + k, k = 0..15
    constexpr std::size_t kTotal = kBase + 4800;
    constexpr std::size_t kGlideBar = AnchorOscillator::kFollowGlideSamples + 16;
    static_assert(kGlideBar == 32);

    const auto report = [](const char* name, std::size_t k, const StepReportT014& rep, std::size_t bar) {
        INFO(name << ", step at grid offset " << k);
        CHECK(rep.latency <= bar);
        CHECK(rep.stays);
        CHECK(rep.bounded);
        return rep.latency;
    };

    SECTION("a step in f0 (a fifth, FOLLOW r = 1.5) is within 5 cents by <= 32 samples") {
        std::size_t worst = 0;
        for (std::size_t k = 0; k < AnchorOscillator::kPitchUpdateInterval; ++k) {
            const std::vector<double> f = renderStepT014(AM::Follow, fC2, kR, kBase + k, kTotal,
                                                         [fG2](AnchorOscillator& a) { a.setFrequency(fG2); });
            const StepReportT014 rep =
                analyseStepT014(f, kBase + k, static_cast<double>(kR) * static_cast<double>(fC2),
                                static_cast<double>(kR) * static_cast<double>(fG2), 5.0);
            worst = std::max(worst, report("f0 step", k, rep, 32));
        }
        std::printf("SC-008(b) f0 step (fifth, FOLLOW r 1.5, C2): worst latency %zu samples (bar 32)\n", worst);
    }

    SECTION("a step in r (1 -> 1.5) is within 5 cents by <= kFollowGlideSamples + 16 samples, no overshoot") {
        const double oldHz = static_cast<double>(fC2);
        const double newHz = static_cast<double>(kR) * static_cast<double>(fC2);
        std::size_t worstSetter = 0;
        std::size_t worstTraj = 0;
        for (std::size_t k = 0; k < AnchorOscillator::kPitchUpdateInterval; ++k) {
            const std::size_t at = kBase + k;
            const std::vector<double> fSet = renderStepT014(AM::Follow, fC2, 1.0f, at, kTotal,
                                                            [&](AnchorOscillator& a) { a.setFollowRatio(kR); });
            worstSetter = std::max(
                worstSetter, report("r step (setFollowRatio)", k, analyseStepT014(fSet, at, oldHz, newHz, 5.0),
                                    kGlideBar));
            std::vector<float> rTraj(kTotal, 1.0f);
            std::fill(rTraj.begin() + static_cast<std::ptrdiff_t>(at), rTraj.end(), kR);
            const std::vector<double> fTraj =
                renderStepT014(AM::Follow, fC2, 1.0f, at, kTotal, [](AnchorOscillator&) {}, nullptr, rTraj.data());
            worstTraj = std::max(worstTraj, report("r step (followRatioPerSample)", k,
                                                   analyseStepT014(fTraj, at, oldHz, newHz, 5.0), kGlideBar));
        }
        std::printf("SC-008(b) r step 1 -> 1.5 (C2): worst latency %zu samples via setFollowRatio, %zu via "
                    "followRatioPerSample (bar %zu)\n",
                    worstSetter, worstTraj, kGlideBar);
    }

    SECTION("the glide is linear in delta over kFollowGlideSamples from the grid sample (plan S4.10)") {
        constexpr std::size_t kAt = kBase + 5;
        const std::vector<double> f =
            renderStepT014(AM::Follow, fC2, 1.0f, kAt, kTotal, [&](AnchorOscillator& a) { a.setFollowRatio(kR); });
        const std::size_t g = kBase + AnchorOscillator::kPitchUpdateInterval;   // the first grid sample after kAt
        const double fFrom = f[g - 1];
        const double fTo = f[g + AnchorOscillator::kFollowGlideSamples + 4];
        double worst = 0.0;
        for (std::size_t k = 0; k < AnchorOscillator::kFollowGlideSamples; ++k) {
            const double expected = fFrom + (fTo - fFrom) * static_cast<double>(k + 1)
                                                / static_cast<double>(AnchorOscillator::kFollowGlideSamples);
            worst = std::max(worst, std::abs(f[g + k] - expected));
        }
        std::printf("SC-008(b) glide shape: worst |f_inst - linear law| %.3e Hz\n", worst);
        CHECK(worst <= 1e-6);
        CHECK(std::abs(centsT014(fTo, static_cast<double>(kR) * static_cast<double>(fC2))) <= 0.01);
        bool still = true;
        for (std::size_t n = kAt; n < g; ++n)
            still = still && std::abs(f[n] - fFrom) <= 1e-6;   // nothing moves before the grid sample
        CHECK(still);
    }

    SECTION("a Reset noteOn snaps the r glide (FR-006, plan S4.6)") {
        const std::vector<float> fresh = [&] {
            AnchorOscillator a;
            a.prepare(kFsT014);
            a.setControls(envControlsT013(AM::Follow, 6.0f, 40.0f, 15.0f));
            a.setFollowRatio(kR);
            a.setRetriggerPhase(ARP::Reset);
            a.noteOn(fC2);
            return PT::renderAnchor(a, 4800, 1).L;
        }();
        AnchorOscillator a;
        a.prepare(kFsT014);
        a.setControls(envControlsT013(AM::Follow, 6.0f, 40.0f, 15.0f));
        a.setRetriggerPhase(ARP::Reset);
        a.noteOn(fC2);
        (void)PT::renderAnchor(a, 4807, 64);   // FOLLOW at r = 1
        a.setFollowRatio(kR);
        a.noteOn(fC2);
        const std::vector<float> after = PT::renderAnchor(a, 4800, 1).L;
        CHECK(std::memcmp(after.data(), fresh.data(), fresh.size() * sizeof(float)) == 0);
    }
}

TEST_CASE("AnchorOscillator_FollowEqualsLockAtUnity", "[systems][profundum][anchor]") {   // SC-008(c)
    const float fC2 = PT::anchorMidiHz(36);
    const auto total = static_cast<std::size_t>(std::llround(3.0 * kFsT014));
    const std::vector<float> traj = vibratoBendT014(fC2, kFsT014, total);
    const std::vector<float> unity(total, 1.0f);
    std::size_t compared = 0;
    for (const AW w : {AW::Sine, AW::Triangle}) {
        for (const std::size_t block : {std::size_t{37}, std::size_t{512}}) {
            for (const bool useTraj : {false, true}) {
                const float* tp = useTraj ? traj.data() : nullptr;
                const std::vector<float> lock = renderModeT014(AM::Lock, w, fC2, 1.0f, total, block, tp);
                const std::vector<float> follow = renderModeT014(AM::Follow, w, fC2, 1.0f, total, block, tp);
                const std::vector<float> followTraj =
                    renderModeT014(AM::Follow, w, fC2, 1.0f, total, block, tp, unity.data());
                INFO("waveform " << static_cast<int>(w) << " block " << block
                                 << (useTraj ? " vibrato+bend trajectory" : " static C2"));
                CHECK(std::memcmp(lock.data(), follow.data(), total * sizeof(float)) == 0);
                CHECK(std::memcmp(lock.data(), followTraj.data(), total * sizeof(float)) == 0);
                compared += 2;
            }
        }
    }
    // Positive control: FOLLOW at r = 1.5 is not LOCK.
    const std::vector<float> lockR = renderModeT014(AM::Lock, AW::Sine, fC2, 1.5f, 9600, 512);
    const std::vector<float> followR = renderModeT014(AM::Follow, AW::Sine, fC2, 1.5f, 9600, 512);
    CHECK_FALSE(std::memcmp(lockR.data(), followR.data(), lockR.size() * sizeof(float)) == 0);
    std::printf("SC-008(c): %zu FOLLOW r = 1 renders bit-identical to LOCK\n", compared);
}

TEST_CASE("AnchorOscillator_TrajectoryContract", "[systems][profundum][anchor]") {   // SC-025
    for (const int midi : {28, 36}) {   // E1, C2
        const float f0 = PT::anchorMidiHz(midi);
        const double hz = static_cast<double>(f0);
        INFO("midi " << midi);

        // (1) Note pitch change: a fifth up via setFrequency.
        {
            const float fNew = PT::anchorMidiHz(midi + 7);
            constexpr std::size_t kAt = 4807;   // off the grid
            constexpr std::size_t kTotal = kAt + 4800;
            for (const AM m : {AM::Lock, AM::Follow}) {
                const std::vector<double> f = renderStepT014(m, f0, 1.0f, kAt, kTotal,
                                                             [fNew](AnchorOscillator& a) { a.setFrequency(fNew); });
                const StepReportT014 rep = analyseStepT014(f, kAt, hz, static_cast<double>(fNew), 1.0);
                std::printf("SC-025(1) midi %d %s: fifth up within 1 cent after %zu samples (bar 32)\n", midi,
                            modeNameT013(m), rep.latency);
                INFO(modeNameT013(m));
                CHECK(rep.latency <= 32);
                CHECK(rep.stays);
                CHECK(rep.bounded);
            }
        }

        // (2) Glide: 200 ms, ±7 semitones; both modes within ±5 cents from 50 ms after its end.
        {
            constexpr std::size_t kFrom = 4800;
            const auto len = static_cast<std::size_t>(std::llround(0.2 * kFsT014));
            const auto settle = static_cast<std::size_t>(std::llround(0.05 * kFsT014));
            const std::size_t total = kFrom + len + settle + 4800;
            for (const double semis : {7.0, -7.0}) {
                const std::vector<float> traj = glideTrajT013(f0, semis, total, kFrom, len);
                for (const AM m : {AM::Lock, AM::Follow}) {
                    const std::vector<double> f =
                        renderStepT014(m, f0, 1.0f, 1, total, [](AnchorOscillator&) {}, traj.data());
                    double worst = 0.0;
                    for (std::size_t n = kFrom + len + settle; n < total; ++n)
                        worst = std::max(worst, std::abs(centsT014(f[n], static_cast<double>(traj.back()))));
                    std::printf("SC-025(2) midi %d %s glide %+.0f st: worst %.4f cents from 50 ms after the end "
                                "(bar 5)\n",
                                midi, modeNameT013(m), semis, worst);
                    INFO(modeNameT013(m) << " glide " << semis);
                    CHECK(worst <= 5.0);
                }
            }
        }

        // (3) Drop: −12 semitones over 100 ms and back. The core is fed the dropping trajectory; LOCK
        // is fed the pre-Drop (resolved, constant) trajectory, FOLLOW (r = 1.5) the post-Drop one.
        {
            constexpr float kR = 1.5f;
            const auto dropFrom = static_cast<std::size_t>(std::llround(2.0 * kFsT014));
            const auto dropLen = static_cast<std::size_t>(std::llround(0.1 * kFsT014));
            const std::size_t total = dropFrom + dropLen + 9600;
            const std::vector<float> drop = dropTrajectory(f0, total, dropFrom, dropLen);
            const std::vector<float> resolved(total, f0);
            REQUIRE(*std::min_element(drop.begin(), drop.end()) < 0.51f * f0);   // the Drop really drops

            auto core = std::make_unique<ProfundumCore>();
            core->prepare(kFsT014);
            const PT::Render cr = PT::renderCore(*core, PT::midGrid(), f0, static_cast<double>(total) / kFsT014,
                                                 ProfundumCore::RetriggerPhase::Reset, false, 32, drop.data());
            PT::requireLREqual(cr.L.data(), cr.R.data(), cr.L.size());
            REQUIRE(cr.L.size() == total);

            // LOCK on the resolved trajectory: f_inst = the note f0 within 1 cent at every Drop sample.
            AnchorOscillator lockA;
            lockA.prepare(kFsT014);
            lockA.setControls(envControlsT013(AM::Lock, 6.0f, 40.0f, 15.0f));
            lockA.setFollowRatio(kR);
            lockA.setRetriggerPhase(ARP::Reset);
            lockA.noteOn(f0);
            const PT::AnchorRender lr = PT::renderAnchor(lockA, total, 1, resolved.data(), nullptr, true);
            REQUIRE(lockA.stateFinite());
            const std::vector<double> fl = instFreqT014(lr.phase, kFsT014);
            double worstLock = 0.0;
            for (std::size_t n = dropFrom; n < dropFrom + dropLen; ++n)
                worstLock = std::max(worstLock, std::abs(centsT014(fl[n], hz)));

            // SC-003's static bar on the 1 s window (whole periods) before the Drop begins.
            const auto L = static_cast<std::size_t>(std::llround(std::ceil(hz) * kFsT014 / hz));
            REQUIRE(L <= dropFrom);
            const std::size_t start = dropFrom - L;
            const double ta = PT::goertzelPhase(lr.L.data() + start, L, kFsT014, hz);
            const double tc = PT::goertzelPhase(cr.L.data() + start, L, kFsT014, hz);
            const double dTheta = wrapDegT009((ta - tc) * kRadToDegT009);

            // FOLLOW on the post-Drop trajectory: 1.5·f0(grid t) within 5 cents.
            AnchorOscillator followA;
            followA.prepare(kFsT014);
            followA.setControls(envControlsT013(AM::Follow, 6.0f, 40.0f, 15.0f));
            followA.setFollowRatio(kR);
            followA.setRetriggerPhase(ARP::Reset);
            followA.noteOn(f0);
            const PT::AnchorRender fr = PT::renderAnchor(followA, total, 1, drop.data(), nullptr, true);
            REQUIRE(followA.stateFinite());
            const std::vector<double> ff = instFreqT014(fr.phase, kFsT014);
            double worstFollow = 0.0;
            for (std::size_t n = 32; n < total; ++n) {
                const std::size_t grid = n - n % AnchorOscillator::kPitchUpdateInterval;
                const double expected = static_cast<double>(kR) * static_cast<double>(drop[grid]);
                worstFollow = std::max(worstFollow, std::abs(centsT014(ff[n], expected)));
            }
            // Teeth: FOLLOW really follows the Drop (its lowest f_inst is near 1.5·f0/2).
            const double fMin = *std::min_element(ff.begin() + static_cast<std::ptrdiff_t>(dropFrom),
                                                  ff.begin() + static_cast<std::ptrdiff_t>(dropFrom + dropLen));

            std::printf("SC-025(3) midi %d: LOCK worst %.4f cents during the Drop (bar 1); dTheta before the Drop "
                        "%+.6f deg (bar 1); FOLLOW worst %.4f cents re 1.5*f0(grid t) (bar 5), lowest %.3f Hz\n",
                        midi, worstLock, dTheta, worstFollow, fMin);
            CHECK(worstLock <= 1.0);
            CHECK(std::abs(dTheta) <= 1.0);
            CHECK(worstFollow <= 5.0);
            CHECK(std::abs(centsT014(fMin, 0.5 * static_cast<double>(kR) * hz)) <= 30.0);
        }
    }
}

// =============================================================================
// T019 SC-013 (FR-051): per-voice independence and the static-state source scan
// =============================================================================

namespace {

struct IndepVoiceT019 {
    float f0;
    ARP policy;
    AM mode;
    AW wave;
};

void startVoiceT019(AnchorOscillator& a, const IndepVoiceT019& v) {
    a.setControls(controlsT019(v.wave, v.mode, 0.8f));
    a.setRetriggerPhase(v.policy);
    a.noteOn(v.f0);
}

/// Mid-render event: a waveform switch and a re-arming noteOn a fifth up (A: a Reset snap; B: a
/// FreeRunning bridged PUNCH re-arm).
void midEventT019(AnchorOscillator& a, const IndepVoiceT019& v) {
    a.setControls(controlsT019(nextWaveT019(v.wave), v.mode, 0.5f));
    a.noteOn(v.f0 * 1.5f, true);
}

}  // namespace

TEST_CASE("AnchorOscillator_InstancesIndependent", "[systems][profundum][anchor]") {
    constexpr double kFs = 48000.0;
    constexpr std::size_t kSeg = 4800;
    const IndepVoiceT019 va{.f0 = PT::anchorMidiHz(36), .policy = ARP::Reset, .mode = AM::Lock, .wave = AW::Triangle};
    const IndepVoiceT019 vb{
        .f0 = PT::anchorMidiHz(28), .policy = ARP::FreeRunning, .mode = AM::Punch, .wave = AW::AsymmetricSine};

    const auto alone = [&](const IndepVoiceT019& v, std::size_t block) {
        AnchorOscillator a;
        a.prepare(kFs);
        startVoiceT019(a, v);
        PT::AnchorRender r = PT::renderAnchor(a, kSeg, block);
        midEventT019(a, v);
        const PT::AnchorRender r2 = PT::renderAnchor(a, kSeg, block);
        REQUIRE(a.stateFinite());
        r.L.insert(r.L.end(), r2.L.begin(), r2.L.end());
        return std::move(r.L);
    };

    for (const std::size_t block : {std::size_t{1}, std::size_t{37}, std::size_t{512}}) {
        const std::vector<float> refA = alone(va, block);
        const std::vector<float> refB = alone(vb, block);
        REQUIRE(anyNonZeroT019(refA));
        REQUIRE(anyNonZeroT019(refB));
        REQUIRE_FALSE(sameFloatsT019(refA, refB));   // the two voices really differ

        AnchorOscillator a;
        AnchorOscillator b;
        a.prepare(kFs);
        b.prepare(kFs);
        startVoiceT019(a, va);
        startVoiceT019(b, vb);
        std::vector<float> aL(2 * kSeg, 0.0f);
        std::vector<float> aR(2 * kSeg, 0.0f);
        std::vector<float> bL(2 * kSeg, 0.0f);
        std::vector<float> bR(2 * kSeg, 0.0f);
        const auto interleave = [&](std::size_t from) {
            for (std::size_t pos = from; pos < from + kSeg; pos += block) {
                const std::size_t n = std::min(block, from + kSeg - pos);
                a.processBlock(aL.data() + pos, aR.data() + pos, n);
                b.processBlock(bL.data() + pos, bR.data() + pos, n);
            }
        };
        interleave(0);
        midEventT019(a, va);
        midEventT019(b, vb);
        interleave(kSeg);

        const bool okA = sameFloatsT019(aL, refA) && sameFloatsT019(aR, refA);
        const bool okB = sameFloatsT019(bL, refB) && sameFloatsT019(bR, refB);
        std::printf("SC-013 block %zu: A (C2, Reset, LOCK, triangle) interleaved %s alone; B (E1, FreeRunning, PUNCH, "
                    "asymmetric) interleaved %s alone\n",
                    block, okA ? "==" : "!=", okB ? "==" : "!=");
        INFO("block " << block);
        CHECK(okA);
        CHECK(okB);
    }

    // Source scan (deviation 3: the header path from KRATE_DSP_TESTS_DIR): no mutable static state.
    const std::string path = std::string(KRATE_DSP_TESTS_DIR) + "/../include/krate/dsp/systems/anchor_oscillator.h";
    std::ifstream in(path, std::ios::binary);
    INFO("header " << path);
    REQUIRE(in.is_open());
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    REQUIRE_FALSE(text.empty());
    const std::vector<std::string> hits = PT::anchorFindMutableStatics(text);
    for (const std::string& h : hits)
        std::printf("SC-013 mutable static in anchor_oscillator.h: %s\n", h.c_str());
    std::printf("SC-013 source scan: %zu bytes scanned, %zu mutable static(s) (bar 0)\n", text.size(), hits.size());
    REQUIRE(hits.empty());
}

// =============================================================================
// T019 SC-016 (FR-056): determinism, block-size independence, rate changes
// =============================================================================

TEST_CASE("AnchorOscillator_Deterministic", "[systems][profundum][anchor]") {
    constexpr double kFs = 48000.0;
    constexpr std::size_t kSeg = 3000;
    const std::vector<float> vib = vibratoT019(PT::anchorMidiHz(36), kFs, 3 * kSeg);

    const auto run = [&](AW w, AM m) {
        AnchorOscillator a;
        a.prepare(kFs);
        std::vector<float> out;
        std::size_t pos = 0;
        transitionScriptT019(a, w, m, ARP::FreeRunning, [&]() {
            const PT::AnchorRender r = PT::renderAnchor(a, kSeg, 37, vib.data() + pos);
            out.insert(out.end(), r.L.begin(), r.L.end());
            pos += kSeg;
        });
        REQUIRE(a.stateFinite());
        return out;
    };

    std::size_t mismatches = 0;
    for (const AW w : kWavesT019) {
        for (const AM m : kModesT019) {
            const std::vector<float> first = run(w, m);
            const std::vector<float> second = run(w, m);
            if (!sameFloatsT019(first, second) || !anyNonZeroT019(first)) {
                ++mismatches;
                std::printf("SC-016 determinism %s / %s: two instances with one call sequence differ\n",
                            waveNameT019(w), modeNameT019(m));
            }
        }
    }
    std::printf("SC-016 determinism: %zu of 20 cells differ (bar 0)\n", mismatches);
    REQUIRE(mismatches == 0);
}

TEST_CASE("AnchorOscillator_BlockSizeIndependent", "[systems][profundum][anchor]") {
    constexpr double kFs = 48000.0;
    constexpr std::size_t kSeg = 18944;   // lcm(512, 37): the mid-render setters sit on a block boundary in every partition
    constexpr std::array<std::size_t, 4> kPartitions{512, 64, 37, 1};
    const float f0 = PT::anchorMidiHz(28);
    const std::vector<float> vib = vibratoT019(f0, kFs, 2 * kSeg);
    std::vector<float> rTraj(2 * kSeg, 1.5f);   // FOLLOW: ratio steps that fall inside host blocks
    for (std::size_t i = 0; i < rTraj.size(); ++i) {
        if (i >= 5003 && i < 9001)
            rTraj[i] = 2.0f;
        else if (i >= 25007)
            rTraj[i] = 0.75f;
    }

    const auto run = [&](AM m, std::size_t block) {
        AnchorOscillator a;
        a.prepare(kFs);
        a.setControls(AnchorOscillator::Controls{.waveform = AW::Triangle,
                                                 .mode = m,
                                                 .level = 0.9f,
                                                 .punchBoostDb = 9.0f,
                                                 .punchTimeMs = 120.0f,
                                                 .ghostTimeMs = 60.0f});
        a.setFollowRatio(1.5f);
        a.setRetriggerPhase(ARP::Reset);
        a.noteOn(f0);
        const float* rt = m == AM::Follow ? rTraj.data() : nullptr;
        PT::AnchorRender r = PT::renderAnchor(a, kSeg, block, vib.data(), rt);
        // Between the two no-setter stretches: a waveform switch, a level step and a FreeRunning re-arm
        // (PUNCH: a bridged arm whose 250 ms fall is pending across many blocks).
        a.setControls(AnchorOscillator::Controls{.waveform = AW::AsymmetricSine,
                                                 .mode = m,
                                                 .level = 0.4f,
                                                 .punchBoostDb = 12.0f,
                                                 .punchTimeMs = 250.0f,
                                                 .ghostTimeMs = 100.0f});
        a.setRetriggerPhase(ARP::FreeRunning);
        a.noteOn(f0 * 1.5f, true);
        const PT::AnchorRender r2 =
            PT::renderAnchor(a, kSeg, block, vib.data() + kSeg, rt != nullptr ? rt + kSeg : nullptr);
        REQUIRE(a.stateFinite());
        r.L.insert(r.L.end(), r2.L.begin(), r2.L.end());
        return std::move(r.L);
    };

    std::size_t mismatches = 0;
    for (const AM m : kModesT019) {
        const std::vector<float> ref = run(m, kPartitions[0]);
        REQUIRE(anyNonZeroT019(ref));
        for (std::size_t p = 1; p < kPartitions.size(); ++p) {
            const std::vector<float> x = run(m, kPartitions[p]);
            const bool same = sameFloatsT019(x, ref);
            std::printf("SC-016 block-size %s: partition %zu %s partition 512\n", modeNameT019(m), kPartitions[p],
                        same ? "==" : "!=");
            if (!same)
                ++mismatches;
        }
    }
    std::printf("SC-016 block-size independence: %zu mismatch(es) over 4 modes x 3 partitions (bar 0)\n", mismatches);
    REQUIRE(mismatches == 0);
}

namespace {

constexpr std::size_t kFrameT019 = Krate::DSP::TestUtils::kLowFrequencyFftSize;   // 262144
constexpr std::size_t kSettleT019 = 262144;                                       // descriptor steady-state render

/// THD = Σ_{n≥2} wn / w1 in dB.
double thdDbT019(const std::vector<double>& pw) {
    REQUIRE(!pw.empty());
    REQUIRE(pw[0] > 0.0);
    double s = 0.0;
    for (std::size_t i = 1; i < pw.size(); ++i)
        s += pw[i];
    return PT::safeDb10(s / pw[0]);
}

/// Waveform w, LOCK, level 1, a1 = kMaxCoreH1Amplitude, r = 1, Reset noteOn f0 on the given (already
/// prepared, possibly sounding) instance; n samples in 512 blocks. A Reset noteOn snaps every stateful
/// control (FR-006, SC-017), so a re-used instance renders as a fresh one.
std::vector<float> renderLockT019(AnchorOscillator& a, AW w, float f0, std::size_t n) {
    AnchorOscillator::Controls c{};
    c.waveform = w;
    c.mode = AM::Lock;
    c.level = 1.0f;
    a.setControls(c);
    a.setCoreH1Amplitude(AnchorOscillator::kMaxCoreH1Amplitude);
    a.setFollowRatio(1.0f);
    a.setRetriggerPhase(ARP::Reset);
    a.noteOn(f0);
    PT::AnchorRender r = PT::renderAnchor(a, n, 512);
    REQUIRE(a.stateFinite());
    return std::move(r.L);
}

}  // namespace

TEST_CASE("AnchorOscillator_SampleRateChange", "[systems][profundum][anchor]") {
    namespace TU = Krate::DSP::TestUtils;
    constexpr std::array<double, 5> kRateSequence{48000.0, 96000.0, 44100.0, 22050.0, 192000.0};
    constexpr int kThdHarmonics = 31;
    const float fE1 = PT::anchorMidiHz(28);
    const double hz = static_cast<double>(fE1);
    const float a1Max = AnchorOscillator::kMaxCoreH1Amplitude;

    auto inst = std::make_unique<AnchorOscillator>();   // ONE instance, re-prepared at every rate
    for (const double fs : kRateSequence) {
        inst->prepare(fs);
        INFO("fs " << fs);

        // ---- SC-001(a) E1 sine smoke, and the fingerprint cross-rate smoke against a fresh instance ----
        const std::vector<float> sine = renderLockT019(*inst, AW::Sine, fE1, kSettleT019 + kFrameT019);
        const std::vector<double> pwSine =
            PT::renderedHarmonicPowers(sine.data() + kSettleT019, fs, hz, kThdHarmonics);
        const double thdSine = thdDbT019(pwSine);
        {
            AnchorOscillator fresh;
            fresh.prepare(fs);
            const std::vector<float> ref = renderLockT019(fresh, AW::Sine, fE1, kSettleT019 + kFrameT019);
            const TU::FingerprintComparison cmp =
                TU::compareFingerprints(TU::fingerprintRender(std::span<const float>(sine)),
                                        TU::fingerprintRender(std::span<const float>(ref)));
            std::printf("SC-016 fs %.0f: E1 sine THD %.3f dB (bar -90); fingerprint vs a fresh instance: metric %.3g, "
                        "sample %.3g %s\n",
                        fs, thdSine, cmp.worstMetricRelativeError, static_cast<double>(cmp.worstSampleError),
                        cmp.detail.c_str());
            CHECK(cmp.withinTolerance());
        }
        CHECK(thdSine <= -90.0);

        // ---- SC-001(b) E1 triangle ratios smoke ----
        {
            const std::vector<float> tri = renderLockT019(*inst, AW::Triangle, fE1, kSettleT019 + kFrameT019);
            const std::vector<double> pw =
                PT::renderedHarmonicPowers(tri.data() + kSettleT019, fs, hz, kThdHarmonics);
            REQUIRE(pw[0] > 0.0);
            double worstOdd = 0.0;
            double worstEven = -400.0;
            for (int n = 2; n <= kThdHarmonics; ++n) {
                const double d = PT::safeDb10(pw[static_cast<std::size_t>(n - 1)] / pw[0]);
                if (n % 2 == 1 && n <= 15) {
                    const double expected = -40.0 * std::log10(static_cast<double>(n));
                    worstOdd = std::max(worstOdd, std::abs(d - expected));
                    INFO("triangle n " << n << ": " << d << " dB, expected " << expected);
                    CHECK(std::abs(d - expected) <= 0.5);
                } else if (n % 2 == 0 && static_cast<double>(n) * hz < 0.5 * fs) {
                    worstEven = std::max(worstEven, d);
                    INFO("triangle even n " << n << ": " << d << " dB");
                    CHECK(d <= -90.0);
                }
            }
            std::printf("SC-016 fs %.0f: E1 triangle worst odd-ratio error %.4f dB (bar 0.5), worst even %.2f dB "
                        "(bar -90)\n",
                        fs, worstOdd, worstEven);
        }

        // ---- SC-002 RMS bar at E1, every waveform ----
        {
            const auto n = static_cast<std::size_t>(std::llround(2.2 * fs));
            const auto from = static_cast<std::size_t>(std::llround(1.0 * fs));
            const double expected = 20.0 * std::log10(static_cast<double>(AnchorOscillator::kAnchorToH1)
                                                      * static_cast<double>(a1Max) / std::sqrt(2.0));
            for (const AW w : kWavesT019) {
                const double fa = w == AW::OctaveDownSine ? 0.5 * hz : hz;
                const std::vector<float> x = renderLockT019(*inst, w, fE1, n);
                const double rms = PT::rmsDbOverPeriods(x.data() + from, n - from, fs, fa, 1.0);
                std::printf("SC-016 fs %.0f: SC-002 %-11s RMS %.4f dB, expected %.4f dB (bar 0.1)\n", fs,
                            waveNameT019(w), rms, expected);
                INFO(waveNameT019(w) << " rms " << rms << " expected " << expected);
                CHECK(std::abs(rms - expected) <= 0.1);
            }
        }

        // ---- SC-003(a) 10 s E1 smoke against a core prepared at the same rate ----
        {
            const auto total = static_cast<std::size_t>(std::llround(11.0 * fs));
            auto core = std::make_unique<ProfundumCore>();
            core->prepare(fs);
            core->setControls(PT::midGrid());
            core->setRetriggerPhase(ProfundumCore::RetriggerPhase::Reset);
            core->noteOn(fE1);
            std::vector<float> cL(total, 0.0f);
            std::vector<float> cR(total, 0.0f);
            for (std::size_t pos = 0; pos < total; pos += 512) {
                const std::size_t n = std::min<std::size_t>(512, total - pos);
                core->processBlock(cL.data() + pos, cR.data() + pos, n);
            }
            inst->setControls(AnchorOscillator::Controls{});
            inst->setCoreH1Amplitude(a1Max);
            inst->setFollowRatio(1.0f);
            inst->setRetriggerPhase(ARP::Reset);
            inst->noteOn(fE1);
            const PT::AnchorRender ar = PT::renderAnchor(*inst, total, 512);
            REQUIRE(inst->stateFinite());

            // One window of whole f0 periods (>= 1 s) centred at t (the chain TU's lockWindowLength).
            const auto win = static_cast<std::size_t>(std::llround(std::ceil(hz) * fs / hz));
            const auto theta = [&](double t) {
                const auto centre = static_cast<std::size_t>(std::llround(t * fs));
                REQUIRE(centre >= win / 2);
                const std::size_t start = centre - win / 2;
                REQUIRE(start + win <= total);
                const double ta = PT::goertzelPhase(ar.L.data() + start, win, fs, hz);
                const double tc = PT::goertzelPhase(cL.data() + start, win, fs, hz);
                return wrapDegT009((ta - tc) * kRadToDegT009);
            };
            const double d1 = theta(1.0);
            const double d10 = theta(10.0);
            const double drift = wrapDegT009(d10 - d1);
            std::printf("SC-016 fs %.0f: SC-003(a) E1 dTheta(1 s) %+.6f deg (bar 1), drift to 10 s %+.6f deg "
                        "(bar 0.5)\n",
                        fs, d1, drift);
            CHECK(std::abs(d1) <= 1.0);
            CHECK(std::abs(drift) <= 0.5);
        }
    }
}

// =============================================================================
// T019 SC-007 matrix (FR-050, FR-062): L == R bitwise, mono sum = 2·L exactly
// =============================================================================

TEST_CASE("AnchorOscillator_LrBitIdentical", "[systems][profundum][anchor]") {
    constexpr double kFs = 48000.0;
    constexpr std::size_t kSeg = 4800;
    std::size_t renders = 0;
    std::size_t lrFailures = 0;
    std::size_t monoFailures = 0;
    std::size_t silent = 0;
    for (const AW w : kWavesT019) {
        for (const AM m : kModesT019) {
            for (const ARP policy : kPoliciesT019) {
                for (const std::size_t block : kHostBlocksT019) {
                    AnchorOscillator a;
                    a.prepare(kFs);
                    std::vector<float> L;
                    std::vector<float> R;
                    L.reserve(3 * kSeg);
                    R.reserve(3 * kSeg);
                    std::vector<float> l(kSeg, 0.0f);
                    std::vector<float> r(kSeg, 0.0f);
                    transitionScriptT019(a, w, m, policy, [&]() {
                        for (std::size_t pos = 0; pos < kSeg; pos += block) {
                            const std::size_t n = std::min(block, kSeg - pos);
                            a.processBlock(l.data() + pos, r.data() + pos, n);
                        }
                        L.insert(L.end(), l.begin(), l.end());
                        R.insert(R.end(), r.begin(), r.end());
                    });
                    ++renders;
                    REQUIRE(a.stateFinite());
                    const char* policyName = policy == ARP::Reset ? "Reset" : "FreeRunning";
                    if (!anyNonZeroT019(L))
                        ++silent;
                    if (!PT::samplesBitEqual(L.data(), R.data(), L.size())) {
                        ++lrFailures;
                        std::printf("SC-007 %s / %s / %s / block %zu: L != R\n", waveNameT019(w), modeNameT019(m),
                                    policyName, block);
                    }
                    std::size_t monoBad = 0;
                    for (std::size_t i = 0; i < L.size(); ++i) {
                        const float monoLL = L[i] + L[i];
                        const float monoLR = L[i] + R[i];
                        const float twice = 2.0f * L[i];
                        if (std::bit_cast<std::uint32_t>(monoLL) != std::bit_cast<std::uint32_t>(monoLR)
                            || std::bit_cast<std::uint32_t>(monoLR) != std::bit_cast<std::uint32_t>(twice))
                            ++monoBad;
                    }
                    if (monoBad != 0) {
                        ++monoFailures;
                        std::printf("SC-007 %s / %s / %s / block %zu: %zu mono-sum sample(s) != 2*L\n",
                                    waveNameT019(w), modeNameT019(m), policyName, block, monoBad);
                    }
                }
            }
        }
    }
    std::printf("SC-007 matrix: %zu renders (5 waveforms x 4 modes x 2 policies x 5 blocks), %zu L != R, %zu mono-sum "
                "failures, %zu silent (bars 0 / 0 / 0 / 0)\n",
                renders, lrFailures, monoFailures, silent);
    REQUIRE(renders == 200);
    REQUIRE(silent == 0);
    REQUIRE(lrFailures == 0);
    REQUIRE(monoFailures == 0);
}
