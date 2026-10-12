// ==============================================================================
// Layer 3: System Tests - Profundum Anchor, core + Fold chain TU
//                                    (Profundum Phase 3, specs/profundum-phase3-anchor)
// ==============================================================================
// Reference: specs/profundum-phase3-anchor/plan.md (S8.1)
//            specs/profundum-phase3-anchor/tasks.md (T006 wires this TU,
//                                                    T009, T011, T012, T018 fill it)
//
// SCOPE OF THIS TU (plan S8.1): SC-003 (all arms), SC-008 (r), SC-009, SC-022,
//   SC-024. Tags [systems][profundum][anchor-chain]; 60 s cases [long].
//   Includes profundum_core.h and harmonic_fold.h to prove they coexist.
// ==============================================================================

#include "anchor_oscillator_test_helpers.h"

#include <krate/dsp/systems/anchor_oscillator.h>
#include <krate/dsp/systems/profundum_core.h>
#include <krate/dsp/processors/harmonic_fold.h>
#include <krate/dsp/processors/spectral_shape_recipe.h>

#include <catch2/catch_test_macros.hpp>

#include <hwy/targets.h>   // SC-003 ISA arm: SupportedAndGeneratedTargets, SetSupportedTargetsForTest

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

using Krate::DSP::AnchorOscillator;
using Krate::DSP::HarmonicFold;
using Krate::DSP::ProfundumCore;
using Krate::DSP::SpectralShapeRecipe;
namespace PT = Krate::DSP::ProfundumTest;

namespace {

using ARP = AnchorOscillator::RetriggerPhase;
using CRP = ProfundumCore::RetriggerPhase;

constexpr double kRadToDeg = 57.295779513082320876798;
constexpr double kStaticBarDeg = 1.0;   // SC-003: |Δθ(1 s)|
constexpr double kDriftBarDeg = 0.5;    // SC-003: |Δθ(T) − Δθ(1 s)|
constexpr std::array<std::size_t, 2> kHostBlocks{37, 512};

// ---- T009 Δθ measurement (tasks.md T009: "Δθ measurement (all arms)") ----

/// Wrap degrees into (−180, 180].
double wrapDeg(double d) {
    d = std::fmod(d, 360.0);
    if (d > 180.0)
        d -= 360.0;
    else if (d <= -180.0)
        d += 360.0;
    return d;
}

/// round(k·fs/f0) samples with k = ceil(f0) periods (≥ 1 s).
std::size_t lockWindowLength(double fs, double f0) {
    return static_cast<std::size_t>(std::llround(std::ceil(f0) * fs / f0));
}

/// Start sample of the lock window centred at t seconds.
std::size_t centredStart(double t, double fs, double f0) {
    const auto centre = static_cast<std::size_t>(std::llround(t * fs));
    const std::size_t half = lockWindowLength(fs, f0) / 2;
    REQUIRE(centre >= half);
    return centre - half;
}

/// Δθ = wrap(θ_a − θ_ref) in degrees, both phases from goertzelPhase at `hz` on the same window.
double deltaThetaDeg(const std::vector<float>& anchor, const std::vector<float>& ref, std::size_t start, double fs,
                     double hz) {
    const std::size_t L = lockWindowLength(fs, hz);
    REQUIRE(start + L <= anchor.size());
    REQUIRE(start + L <= ref.size());
    const double ta = PT::goertzelPhase(anchor.data() + start, L, fs, hz);
    const double tr = PT::goertzelPhase(ref.data() + start, L, fs, hz);
    return wrapDeg((ta - tr) * kRadToDeg);
}

/// SC-003(d): Δθ = wrap(2·θ_a(f0/2) − θ1(f0)) in degrees on one window of whole f0/2 periods
/// (lockWindowLength at f0/2, ≥ 1 s). goertzelPhase returns the cosine phase; each phase is moved to
/// the sine convention (+π/2) before doubling, so a locked sin(φ/2) reads 0 rather than a constant −90°.
double deltaThetaOctaveDeg(const std::vector<float>& anchor, const std::vector<float>& ref, std::size_t start,
                           double fs, double hz) {
    constexpr double kHalfPi = 1.57079632679489661923;
    const double half = 0.5 * hz;
    const std::size_t L = lockWindowLength(fs, half);
    REQUIRE(start + L <= anchor.size());
    REQUIRE(start + L <= ref.size());
    const double ta = PT::goertzelPhase(anchor.data() + start, L, fs, half) + kHalfPi;
    const double tr = PT::goertzelPhase(ref.data() + start, L, fs, hz) + kHalfPi;
    return wrapDeg((2.0 * ta - tr) * kRadToDeg);
}

std::size_t alignUp(std::size_t n, std::size_t block) { return (n + block - 1) / block * block; }

// ---- the paired core + Anchor renderer (identical calls and host blocks) ----

struct ChainNoteOn {
    std::size_t at;   // requested sample; takes effect at the first host-block start >= at
    ARP policy;
    float f0;
};

struct ChainSpec {
    double fs = 48000.0;
    std::size_t block = 512;
    std::size_t total = 0;
    const float* traj = nullptr;        // whole-render f0PerSample, fed to both objects (nullable)
    std::vector<ChainNoteOn> noteOns;   // ascending; the first at 0
    bool excite = false;                // SC-003(e): mix 1, drive 1, asymmetry 1
    bool fold = false;                  // SC-003(e)(ii): HarmonicFold at 100 after the core
    float foldHz = 0.0f;
    AnchorOscillator::Waveform waveform = AnchorOscillator::Waveform::Sine;   // SC-003(d): octave-down
};

struct ChainRender {
    std::vector<float> core;              // core L (L == R asserted)
    std::vector<float> folded;            // HarmonicFold(core) L when spec.fold
    std::vector<float> anchor;            // Anchor L (L == R asserted by renderAnchor)
    std::vector<std::size_t> noteOnAt;    // the sample each noteOn took effect at
};

/// The core (mid grid, excitation off unless spec.excite) and the Anchor (sine, LOCK, default
/// Controls, a1 = kMaxCoreH1Amplitude) receive the same setRetriggerPhase / noteOn / f0PerSample
/// calls and the same host-block partition. A noteOn lands on a host-block boundary for both.
ChainRender renderChain(const ChainSpec& s) {
    REQUIRE(s.total > 0);
    REQUIRE(s.block > 0);
    REQUIRE(!s.noteOns.empty());
    REQUIRE(s.noteOns.front().at == 0);

    auto core = std::make_unique<ProfundumCore>();
    core->prepare(s.fs);
    core->setControls(PT::midGrid());
    if (s.excite)
        core->setExcitation(ProfundumCore::ExcitationControls{.mix = 1.0f, .drive = 1.0f, .asymmetry = 1.0f});
    auto anchor = std::make_unique<AnchorOscillator>();
    anchor->prepare(s.fs);
    if (s.waveform != AnchorOscillator::Waveform::Sine) {
        AnchorOscillator::Controls c{};
        c.waveform = s.waveform;
        anchor->setControls(c);   // latched by the first noteOn (C-8)
    }

    ChainRender r;
    r.core.assign(s.total, 0.0f);
    std::vector<float> coreR(s.total, 0.0f);
    r.anchor.reserve(s.total);

    const auto segment = [&](std::size_t from, std::size_t to) {
        if (to <= from)
            return;
        for (std::size_t p = from; p < to; p += s.block) {
            const std::size_t n = std::min(s.block, to - p);
            core->processBlock(r.core.data() + p, coreR.data() + p, n, s.traj != nullptr ? s.traj + p : nullptr);
        }
        const PT::AnchorRender a =
            PT::renderAnchor(*anchor, to - from, s.block, s.traj != nullptr ? s.traj + from : nullptr);
        r.anchor.insert(r.anchor.end(), a.L.begin(), a.L.end());
    };

    std::size_t pos = 0;
    for (const ChainNoteOn& ev : s.noteOns) {
        const std::size_t at = alignUp(ev.at, s.block);
        REQUIRE(at >= pos);
        REQUIRE(at < s.total);
        segment(pos, at);
        pos = at;
        core->setRetriggerPhase(ev.policy == ARP::Reset ? CRP::Reset : CRP::FreeRunning);
        anchor->setRetriggerPhase(ev.policy);
        core->noteOn(ev.f0);
        anchor->noteOn(ev.f0);
        r.noteOnAt.push_back(at);
    }
    segment(pos, s.total);
    REQUIRE(r.anchor.size() == s.total);
    PT::requireLREqual(r.core.data(), coreR.data(), s.total);   // SC-007: zero pan, L == R
    REQUIRE(anchor->stateFinite());

    if (s.fold) {
        auto fold = std::make_unique<HarmonicFold>();
        fold->prepare(s.fs, 4096);
        fold->setAmount(100.0f);
        fold->setFundamentalHz(s.foldHz);
        r.folded = r.core;
        std::vector<float> foldR = r.core;
        for (std::size_t p = 0; p < s.total; p += s.block) {
            const std::size_t n = std::min(s.block, s.total - p);
            fold->processBlock(r.folded.data() + p, foldR.data() + p, n);
        }
    }
    return r;
}

// ---- SC-003(a) static ----

void runStaticLock(int midi, double fs, std::size_t block, double T, double seconds,
                   AnchorOscillator::Waveform waveform = AnchorOscillator::Waveform::Sine) {
    const float f0 = PT::anchorMidiHz(midi);
    const double hz = static_cast<double>(f0);
    const bool octave = waveform == AnchorOscillator::Waveform::OctaveDownSine;
    ChainSpec s;
    s.fs = fs;
    s.block = block;
    s.total = static_cast<std::size_t>(std::llround(seconds * fs));
    s.noteOns = {ChainNoteOn{.at = 0, .policy = ARP::Reset, .f0 = f0}};
    s.waveform = waveform;
    const ChainRender r = renderChain(s);

    // SC-003(d): the octave-down arm measures 2·θ_a(f0/2) − θ1(f0) on windows of whole f0/2 periods.
    const double windowHz = octave ? 0.5 * hz : hz;
    const auto measure = [&](double t) {
        const std::size_t start = centredStart(t, fs, windowHz);
        return octave ? deltaThetaOctaveDeg(r.anchor, r.core, start, fs, hz)
                      : deltaThetaDeg(r.anchor, r.core, start, fs, hz);
    };
    const double d1 = measure(1.0);
    const double dT = measure(T);
    const double drift = wrapDeg(dT - d1);
    std::printf("SC-003(%s) static %s midi %d fs %.0f block %zu: dTheta(1 s) %+.6f deg, dTheta(%.0f s) %+.6f deg, "
                "drift %+.6f deg (bars %.1f / %.1f)\n",
                octave ? "d" : "a", octave ? "octave-down" : "sine", midi, fs, block, d1, T, dT, drift,
                kStaticBarDeg, kDriftBarDeg);
    CHECK(std::abs(d1) <= kStaticBarDeg);
    CHECK(std::abs(drift) <= kDriftBarDeg);
}

// ---- SC-003(b) trajectory ----

/// Bend starts every 5 s whose bend and the following window stay clear of the window centred at T.
std::vector<double> bendStarts(double T) {
    std::vector<double> starts;
    for (int k = 1; 5.0 * k + 0.2 < T - 0.6; ++k)
        starts.push_back(5.0 * k);
    return starts;
}

/// f0·2^(sin(2π·5·t)/12) plus, at each bend start, a 200 ms raised-cosine bend up and back to
/// +12 semitones (alternating −12).
std::vector<float> lockTrajectory(float f0, double fs, std::size_t total, const std::vector<double>& bends) {
    std::vector<float> traj(total, 0.0f);
    for (std::size_t i = 0; i < total; ++i) {
        const double t = static_cast<double>(i) / fs;
        double semis = std::sin(PT::kTwoPi * 5.0 * t);
        for (std::size_t k = 0; k < bends.size(); ++k) {
            const double tb = bends[k];
            if (t >= tb && t < tb + 0.2) {
                const double sign = (k % 2 == 0) ? 1.0 : -1.0;
                semis += sign * 12.0 * 0.5 * (1.0 - std::cos(PT::kTwoPi * (t - tb) / 0.2));
            }
        }
        traj[i] = static_cast<float>(static_cast<double>(f0) * std::exp2(semis / 12.0));
    }
    return traj;
}

void runTrajectoryLock(int midi, double fs, std::size_t block, double T, double seconds) {
    const float f0 = PT::anchorMidiHz(midi);
    const double hz = static_cast<double>(f0);
    const auto total = static_cast<std::size_t>(std::llround(seconds * fs));
    const std::vector<double> bends = bendStarts(T);
    const std::vector<float> traj = lockTrajectory(f0, fs, total, bends);
    ChainSpec s;
    s.fs = fs;
    s.block = block;
    s.total = total;
    s.traj = traj.data();
    s.noteOns = {ChainNoteOn{.at = 0, .policy = ARP::Reset, .f0 = f0}};
    const ChainRender r = renderChain(s);

    const double d1 = deltaThetaDeg(r.anchor, r.core, centredStart(1.0, fs, hz), fs, hz);
    std::printf("SC-003(b) trajectory midi %d fs %.0f block %zu: dTheta(1 s) %+.6f deg (%zu bends)\n", midi, fs, block,
                d1, bends.size());
    CHECK(std::abs(d1) <= kStaticBarDeg);
    double worst = 0.0;
    for (const double tb : bends) {
        const auto start = static_cast<std::size_t>(std::llround((tb + 0.3) * fs));   // 100 ms after the bend ends
        const double d = deltaThetaDeg(r.anchor, r.core, start, fs, hz);
        const double drift = wrapDeg(d - d1);
        worst = std::max(worst, std::abs(drift));
        std::printf("  after bend at %.0f s: dTheta %+.6f deg, drift %+.6f deg\n", tb, d, drift);
        CHECK(std::abs(drift) <= kDriftBarDeg);
    }
    const double dT = deltaThetaDeg(r.anchor, r.core, centredStart(T, fs, hz), fs, hz);
    const double driftT = wrapDeg(dT - d1);
    worst = std::max(worst, std::abs(driftT));
    std::printf("  dTheta(%.0f s) %+.6f deg, drift %+.6f deg; worst drift %.6f deg (bar %.1f)\n", T, dT, driftT, worst,
                kDriftBarDeg);
    CHECK(std::abs(driftT) <= kDriftBarDeg);
}

// ---- SC-003(e) processed voice ----

void runProcessedVoice(int midi, double fs, std::size_t block, double T, double seconds) {
    const float f0 = PT::anchorMidiHz(midi);
    const double hz = static_cast<double>(f0);
    ChainSpec s;
    s.fs = fs;
    s.block = block;
    s.total = static_cast<std::size_t>(std::llround(seconds * fs));
    s.noteOns = {ChainNoteOn{.at = 0, .policy = ARP::Reset, .f0 = f0}};
    s.excite = true;
    s.fold = true;
    s.foldHz = f0;
    const ChainRender r = renderChain(s);

    const std::size_t s1 = centredStart(1.0, fs, hz);
    const std::size_t sT = centredStart(T, fs, hz);
    const double c1 = deltaThetaDeg(r.anchor, r.core, s1, fs, hz);
    const double cT = deltaThetaDeg(r.anchor, r.core, sT, fs, hz);
    const double f1 = deltaThetaDeg(r.anchor, r.folded, s1, fs, hz);
    const double fT = deltaThetaDeg(r.anchor, r.folded, sT, fs, hz);
    const double cDrift = wrapDeg(cT - c1);
    const double fDrift = wrapDeg(fT - f1);
    // SC-003(e)(ii) is recorded, not gated (user ruling, spec Clarifications Session 2026-10-12): the folded-h1 drift is
    // the Phase 1 inter-partial walk carried through the Fold, not the Anchor (evidence/drift-probe.cpp); (e)(i) stays gated.
    std::printf("SC-003(e) processed midi %d fs %.0f block %zu: (i) core h1: dTheta(1 s) %+.6f deg (Phase 2 h1 "
                "figure 0.07 deg, not gated), drift to %.0f s %+.6f deg; (ii) after Fold 100: dTheta(1 s) %+.6f deg "
                "(not gated), drift %+.6f deg (recorded, not gated: Phase 1 inter-partial walk through the Fold, spec "
                "SC-003(e)(ii) 2026-10-12, evidence/drift-probe.cpp)\n",
                midi, fs, block, c1, T, cDrift, f1, fDrift);
    CHECK(std::abs(cDrift) <= kDriftBarDeg);
}

}  // namespace

// =============================================================================
// SC-003(a): static phase lock against core h1 (FR-030, FR-031, D-1)
// =============================================================================

// SC-003(d) (T011): the octave-down arm, Δθ = wrap(2·θ_a(f0/2) − θ1(f0)), the same bars.
constexpr std::array<AnchorOscillator::Waveform, 2> kLockWaves{AnchorOscillator::Waveform::Sine,
                                                              AnchorOscillator::Waveform::OctaveDownSine};

TEST_CASE("AnchorOscillator_PhaseLockStaticSmoke", "[systems][profundum][anchor-chain]") {
    for (const AnchorOscillator::Waveform w : kLockWaves) {
        for (const std::size_t block : kHostBlocks)
            runStaticLock(28, 48000.0, block, 10.0, 11.0, w);   // E1
    }
}

TEST_CASE("AnchorOscillator_PhaseLockStatic60s", "[systems][profundum][anchor-chain][long]") {
    for (const AnchorOscillator::Waveform w : kLockWaves) {
        for (const int midi : {16, 28, 40, 48}) {   // E0, E1, E2, C3
            for (const double fs : {44100.0, 48000.0, 96000.0}) {
                for (const std::size_t block : kHostBlocks)
                    runStaticLock(midi, fs, block, 60.0, 61.0, w);
            }
        }
    }
}

// =============================================================================
// SC-003(b): trajectory (5 Hz ±1-semitone vibrato, ±12-semitone 200 ms bend every 5 s)
// =============================================================================

TEST_CASE("AnchorOscillator_PhaseLockTrajectorySmoke", "[systems][profundum][anchor-chain]") {
    for (const std::size_t block : kHostBlocks)
        runTrajectoryLock(28, 48000.0, block, 10.0, 11.0);   // E1
}

TEST_CASE("AnchorOscillator_PhaseLockTrajectory", "[systems][profundum][anchor-chain][long]") {
    for (const int midi : {16, 28, 40, 48}) {
        for (const std::size_t block : kHostBlocks)
            runTrajectoryLock(midi, 48000.0, block, 60.0, 61.0);
    }
}

// =============================================================================
// SC-003(c): the lock after a Reset noteOn and after FreeRunning noteOns (same f0, +7 semitones)
// =============================================================================

namespace {

void runRetriggerLock() {
    constexpr double kFs = 48000.0;
    struct Variant {
        const char* name;
        ARP policy;
        int semis;
    };
    const std::array<Variant, 3> variants{{
        {"Reset same f0", ARP::Reset, 0},
        {"FreeRunning same f0", ARP::FreeRunning, 0},
        {"FreeRunning +7", ARP::FreeRunning, 7},
    }};
    for (const int midi : {28, 48}) {   // E1, C3
        const float f0 = PT::anchorMidiHz(midi);
        for (const Variant& v : variants) {
            const float fNew = PT::anchorMidiHz(midi + v.semis);
            const double hz = static_cast<double>(fNew);
            for (const std::size_t block : kHostBlocks) {
                const auto n2 = static_cast<std::size_t>(std::llround(2.0 * kFs));
                ChainSpec s;
                s.fs = kFs;
                s.block = block;
                // noteOn at 2 s; windows start 1 s and 9 s after it (T = 9 s after the noteOn).
                s.total = alignUp(n2, block) + static_cast<std::size_t>(std::llround(9.0 * kFs))
                          + lockWindowLength(kFs, hz) + block;
                s.noteOns = {ChainNoteOn{.at = 0, .policy = ARP::Reset, .f0 = f0},
                             ChainNoteOn{.at = n2, .policy = v.policy, .f0 = fNew}};
                const ChainRender r = renderChain(s);
                REQUIRE(r.noteOnAt.size() == 2);
                const std::size_t on = r.noteOnAt[1];
                const std::size_t start1 = on + static_cast<std::size_t>(std::llround(1.0 * kFs));
                const std::size_t start9 = on + static_cast<std::size_t>(std::llround(9.0 * kFs));
                const double d1 = deltaThetaDeg(r.anchor, r.core, start1, kFs, hz);
                const double d9 = deltaThetaDeg(r.anchor, r.core, start9, kFs, hz);
                const double drift = wrapDeg(d9 - d1);
                std::printf("SC-003(c) %s midi %d block %zu (noteOn at sample %zu): dTheta(+1 s) %+.6f deg, "
                            "dTheta(+9 s) %+.6f deg, drift %+.6f deg (bars %.1f / %.1f)\n",
                            v.name, midi, block, on, d1, d9, drift, kStaticBarDeg, kDriftBarDeg);
                CHECK(std::abs(d1) <= kStaticBarDeg);
                CHECK(std::abs(drift) <= kDriftBarDeg);
            }
        }
    }
}

}  // namespace

TEST_CASE("AnchorOscillator_PhaseLockRetrigger", "[systems][profundum][anchor-chain]") { runRetriggerLock(); }

// =============================================================================
// SC-003(e): processed voice (excitation mix 1 / drive 1 / asymmetry 1; then HarmonicFold at 100)
// =============================================================================

TEST_CASE("ProfundumAnchor_PhaseLockProcessedVoiceSmoke", "[systems][profundum][anchor-chain]") {
    for (const std::size_t block : kHostBlocks)
        runProcessedVoice(28, 48000.0, block, 10.0, 11.0);   // E1
}

TEST_CASE("ProfundumAnchor_PhaseLockProcessedVoice", "[systems][profundum][anchor-chain][long]") {
    for (const int midi : {16, 28, 40, 48}) {
        for (const std::size_t block : kHostBlocks)
            runProcessedVoice(midi, 48000.0, block, 60.0, 61.0);
    }
}

// =============================================================================
// SC-003 ISA arm: every bar on every Highway target this CPU supports (D-1, FR-032)
// =============================================================================
// The reference h1 is the bank's float, ISA-dispatched MCF (harmonic_oscillator_bank.h processMcfBatchSIMD ->
// HWY_DYNAMIC_DISPATCH, harmonic_oscillator_bank_simd.cpp). Without a mock a runner only measures the one target it
// dispatches to. hwy::SetSupportedTargetsForTest restricts dispatch to one target at a time, so each runner measures
// every x86 target it supports (SSE2/SSSE3/SSE4/AVX2/AVX3...) and the ARM runner measures its NEON/SVE targets.

namespace {

/// Restricts Highway dispatch to one target; restores real detection on scope exit (also on a failed REQUIRE).
class HwyTargetMock {
public:
    explicit HwyTargetMock(std::int64_t target) { hwy::SetSupportedTargetsForTest(target); }
    ~HwyTargetMock() { hwy::SetSupportedTargetsForTest(0); }
    HwyTargetMock(const HwyTargetMock&) = delete;
    HwyTargetMock& operator=(const HwyTargetMock&) = delete;
};

/// FNV-1a over the bits of a 1 s E1 core render: shows the mock reached the bank's dispatch (the hash moves with
/// the target's rounding) without pinning any value.
std::uint64_t coreRenderHash() {
    ChainSpec s;
    s.total = 48000;
    s.noteOns = {ChainNoteOn{.at = 0, .policy = ARP::Reset, .f0 = PT::anchorMidiHz(28)}};
    const ChainRender r = renderChain(s);
    std::uint64_t h = 1469598103934665603ULL;
    for (const float x : r.core) {
        const auto bits = std::bit_cast<std::uint32_t>(x);
        for (int b = 0; b < 4; ++b) {
            h ^= (bits >> (8 * b)) & 0xFFu;
            h *= 1099511628211ULL;
        }
    }
    return h;
}

template <typename Body>
void forEachIsa(const Body& body) {   // const ref: called once per target, never forwarded
    const std::vector<std::int64_t> targets = hwy::SupportedAndGeneratedTargets();
    REQUIRE(!targets.empty());
    for (const std::int64_t t : targets) {
        const HwyTargetMock mock(t);
        std::printf("SC-003 ISA %s: core render hash %016llx\n", hwy::TargetName(t),
                    static_cast<unsigned long long>(coreRenderHash()));
        INFO("Highway target " << hwy::TargetName(t));
        body();
    }
}

}  // namespace

TEST_CASE("AnchorOscillator_PhaseLockEveryIsaSmoke", "[systems][profundum][anchor-chain]") {
    forEachIsa([] {
        for (const std::size_t block : kHostBlocks) {
            for (const AnchorOscillator::Waveform w : kLockWaves)
                runStaticLock(28, 48000.0, block, 10.0, 11.0, w);   // (a), (d) at E1
            runTrajectoryLock(28, 48000.0, block, 10.0, 11.0);      // (b)
            runProcessedVoice(28, 48000.0, block, 10.0, 11.0);      // (e)(i) gated, (e)(ii) recorded
        }
        runRetriggerLock();                                          // (c)
    });
}

TEST_CASE("AnchorOscillator_PhaseLockEveryIsa60s", "[systems][profundum][anchor-chain][long]") {
    forEachIsa([] {
        for (const int midi : {16, 28, 40, 48}) {   // E0, E1, E2, C3
            for (const std::size_t block : kHostBlocks) {
                for (const double fs : {44100.0, 48000.0, 96000.0}) {
                    for (const AnchorOscillator::Waveform w : kLockWaves)
                        runStaticLock(midi, fs, block, 60.0, 61.0, w);   // (a), (d)
                }
                runTrajectoryLock(midi, 48000.0, block, 60.0, 61.0);   // (b)
                runProcessedVoice(midi, 48000.0, block, 60.0, 61.0);   // (e)
            }
        }
    });
}

// =============================================================================
// SC-024 (a)(b): the Anchor's level against the rendered core h1 (T012; FR-022–FR-024, plan S4.9, S1 C-2)
// SC-024 (c): the summed RMS rise at kDefaultLevel, gated at the ratified <= 3.6 dB (T018; plan S1 C-2)
// =============================================================================

namespace {

struct LevelCalRender {
    std::vector<float> coreL, coreR;
    std::vector<float> anchorFull;      // sine, LOCK, level 1
    std::vector<float> anchorDefault;   // sine, LOCK, kDefaultLevel
    float a1 = 0.0f;                    // the a1 fed with the last host block
};

/// The core (Controls c) and two Anchors (sine, LOCK, level 1 and kDefaultLevel) receive the same Reset
/// noteOn at sample 0 and the same host blocks. After each core block, a1 = deliveredGains()[0] ·
/// kCenterPanGain (plan S4.9, option (a) of S1 C-2) is fed to both Anchors by setCoreH1Amplitude before
/// their block: once per host block.
LevelCalRender renderLevelCal(const SpectralShapeRecipe::Controls& c, float f0, double fs, std::size_t total,
                              std::size_t block) {
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(fs);
    core->setControls(c);
    core->setRetriggerPhase(CRP::Reset);
    core->noteOn(f0);

    const std::array<float, 2> levels{1.0f, AnchorOscillator::kDefaultLevel};
    std::array<std::unique_ptr<AnchorOscillator>, 2> anchors{std::make_unique<AnchorOscillator>(),
                                                             std::make_unique<AnchorOscillator>()};
    for (std::size_t i = 0; i < anchors.size(); ++i) {
        anchors[i]->prepare(fs);
        AnchorOscillator::Controls ac{};
        ac.waveform = AnchorOscillator::Waveform::Sine;
        ac.mode = AnchorOscillator::Mode::Lock;
        ac.level = levels[i];
        anchors[i]->setControls(ac);
        anchors[i]->setRetriggerPhase(ARP::Reset);
        anchors[i]->noteOn(f0);
    }

    LevelCalRender r;
    r.coreL.assign(total, 0.0f);
    r.coreR.assign(total, 0.0f);
    r.anchorFull.assign(total, 0.0f);
    r.anchorDefault.assign(total, 0.0f);
    std::vector<float> fullR(total, 0.0f);
    std::vector<float> defaultR(total, 0.0f);
    for (std::size_t p = 0; p < total; p += block) {
        const std::size_t n = std::min(block, total - p);
        core->processBlock(r.coreL.data() + p, r.coreR.data() + p, n);
        r.a1 = core->deliveredGains()[0] * SpectralShapeRecipe::kCenterPanGain;
        anchors[0]->setCoreH1Amplitude(r.a1);
        anchors[0]->processBlock(r.anchorFull.data() + p, fullR.data() + p, n);
        anchors[1]->setCoreH1Amplitude(r.a1);
        anchors[1]->processBlock(r.anchorDefault.data() + p, defaultR.data() + p, n);
    }
    PT::requireLREqual(r.coreL.data(), r.coreR.data(), total);
    PT::requireLREqual(r.anchorFull.data(), fullR.data(), total);
    PT::requireLREqual(r.anchorDefault.data(), defaultR.data(), total);
    REQUIRE(anchors[0]->stateFinite());
    REQUIRE(anchors[1]->stateFinite());
    return r;
}

/// w1 of the summed buffer core[from + i] + anchor[from + i] over one kLowFrequencyFftSize frame.
double summedW1(const std::vector<float>& core, const std::vector<float>& anchor, std::size_t from, double fs,
                double f0) {
    constexpr std::size_t kFrame = Krate::DSP::TestUtils::kLowFrequencyFftSize;
    std::vector<float> sum(kFrame, 0.0f);
    for (std::size_t i = 0; i < kFrame; ++i)
        sum[i] = core[from + i] + anchor[from + i];
    return PT::renderedHarmonicPowers(sum.data(), fs, f0, 1)[0];
}

// plan S1 C-2 (ratified 2026-10-11): SC-024(c) bound on the RMS rise at kDefaultLevel, every listed state.
constexpr double kDefaultLevelRiseBoundDb = 3.6;

}  // namespace

TEST_CASE("ProfundumAnchor_LevelCalibration", "[systems][profundum][anchor-chain]") {
    constexpr double kFs = 48000.0;
    constexpr std::size_t kBlock = 512;
    constexpr std::size_t kFrame = Krate::DSP::TestUtils::kLowFrequencyFftSize;
    const auto from = static_cast<std::size_t>(std::llround(1.0 * kFs));   // measured after 1 s
    const std::size_t total = alignUp(from + kFrame, kBlock);
    const double k = static_cast<double>(AnchorOscillator::kAnchorToH1);
    const double kDefault = static_cast<double>(AnchorOscillator::kDefaultLevel);

    // The spec's "mid grid" row is kDefaultControls: assert it is the same state as midGrid().
    const SpectralShapeRecipe::Controls mid = PT::midGrid();
    const SpectralShapeRecipe::Controls& def = SpectralShapeRecipe::kDefaultControls;
    REQUIRE(def.depth == mid.depth);
    REQUIRE(def.body == mid.body);
    REQUIRE(def.bodyCurvature == mid.bodyCurvature);
    REQUIRE(def.bodyEmphasis == mid.bodyEmphasis);
    REQUIRE(def.edge == mid.edge);
    REQUIRE(def.shift == mid.shift);

    struct State {
        const char* name;
        const SpectralShapeRecipe::Controls* controls;
    };
    const std::array<State, 6> states{{
        {"kSineAnchor", &SpectralShapeRecipe::kSineAnchor},
        {"kSawAnchor", &SpectralShapeRecipe::kSawAnchor},
        {"kHeavy", &SpectralShapeRecipe::kHeavy},
        {"kHollow", &SpectralShapeRecipe::kHollow},
        {"kGrowl", &SpectralShapeRecipe::kGrowl},
        {"kDefaultControls", &SpectralShapeRecipe::kDefaultControls},
    }};

    for (const int midi : {28, 36}) {   // E1, C2
        const float f0 = PT::anchorMidiHz(midi);
        const auto f0d = static_cast<double>(f0);
        std::vector<double> ratiosDb;
        for (const State& st : states) {
            const LevelCalRender r = renderLevelCal(*st.controls, f0, kFs, total, kBlock);
            const auto a1 = static_cast<double>(r.a1);
            REQUIRE(a1 > 0.0);

            // (a) a1 vs the rendered core h1, by ratio against a test-local float sine of amplitude a1.
            std::vector<float> ref(kFrame, 0.0f);
            for (std::size_t i = 0; i < kFrame; ++i)
                ref[i] = static_cast<float>(a1 * std::sin(2.0 * PT::kAnchorPiD * f0d * static_cast<double>(i) / kFs));
            const double w1Ref = PT::renderedHarmonicPowers(ref.data(), kFs, f0d, 1)[0];
            const double w1CoreL = PT::renderedHarmonicPowers(r.coreL.data() + from, kFs, f0d, 1)[0];
            const double w1CoreR = PT::renderedHarmonicPowers(r.coreR.data() + from, kFs, f0d, 1)[0];
            const double coreVsRefL = PT::safeDb10(w1CoreL / w1Ref);
            const double coreVsRefR = PT::safeDb10(w1CoreR / w1Ref);

            // Anchor RMS (level 1) / (a1/√2) = kAnchorToH1.
            const double rmsDb = PT::rmsDbOverPeriods(r.anchorFull.data() + from, kFrame, kFs, f0d, 1.0);
            const double ratioDb = rmsDb - 20.0 * std::log10(a1 / std::sqrt(2.0));
            ratiosDb.push_back(ratioDb);

            // (b) in-phase sum (same Reset calls): the w1 rise is 20·log10(1 + level·k).
            const double riseFull = PT::safeDb10(summedW1(r.coreL, r.anchorFull, from, kFs, f0d) / w1CoreL);
            const double riseDefault = PT::safeDb10(summedW1(r.coreL, r.anchorDefault, from, kFs, f0d) / w1CoreL);
            const double expectFull = 20.0 * std::log10(1.0 + k);
            const double expectDefault = 20.0 * std::log10(1.0 + kDefault * k);

            std::printf("SC-024 %-16s midi %d: a1 %.6f; (a) core h1 vs ref L %+.4f dB R %+.4f dB (bar 0.1), "
                        "anchor RMS/(a1/sqrt2) %+.4f dB (expected %+.4f, bar 0.1); (b) w1 rise level 1 %.4f dB "
                        "(expected %.4f), kDefaultLevel %.4f dB (expected %.4f) (bar 0.25)\n",
                        st.name, midi, a1, coreVsRefL, coreVsRefR, ratioDb, 20.0 * std::log10(k), riseFull,
                        expectFull, riseDefault, expectDefault);
            INFO(st.name << " midi " << midi);
            CHECK(std::abs(coreVsRefL) <= 0.1);
            CHECK(std::abs(coreVsRefR) <= 0.1);
            CHECK(std::abs(ratioDb - 20.0 * std::log10(k)) <= 0.1);
            CHECK(std::abs(riseFull - expectFull) <= 0.25);
            CHECK(std::abs(riseDefault - expectDefault) <= 0.25);

            // (c) T018, plan S1 C-2 (ratified 2026-10-11, spec SC-024(c)): the summed-signal RMS rise at
            // kDefaultLevel over the core alone, both over the same whole periods (>= 1 s) of one frame.
            std::vector<float> summedDefault(kFrame, 0.0f);
            for (std::size_t i = 0; i < kFrame; ++i)
                summedDefault[i] = r.coreL[from + i] + r.anchorDefault[from + i];
            const double rmsCoreDb = PT::rmsDbOverPeriods(r.coreL.data() + from, kFrame, kFs, f0d, 1.0);
            const double rmsSumDb = PT::rmsDbOverPeriods(summedDefault.data(), kFrame, kFs, f0d, 1.0);
            const double rmsRiseDb = rmsSumDb - rmsCoreDb;
            std::printf("SC-024(c) %-16s midi %d: RMS rise at kDefaultLevel over the core alone %+.4f dB "
                        "(bar <= %.1f dB; model 0.00 ... 3.52 dB)\n",
                        st.name, midi, rmsRiseDb, kDefaultLevelRiseBoundDb);
            CHECK(rmsRiseDb <= kDefaultLevelRiseBoundDb);
        }
        const auto [lo, hi] = std::minmax_element(ratiosDb.begin(), ratiosDb.end());
        std::printf("SC-024(a) midi %d: anchor RMS/(a1/sqrt2) spread across the six states %.4f dB (bar 0.1)\n", midi,
                    *hi - *lo);
        CHECK(*hi - *lo <= 0.1);
    }
}

// =============================================================================
// SC-024(d): no compensation (T012; FR-025, plan S1 C-13)
// =============================================================================
// plan S1 C-13: this is a STRUCTURAL assertion in Phase 3. ProfundumCore and AnchorOscillator share no
// state and the test performs the sum itself, so both checks hold by construction; they are kept as a
// documented tautology, not a gate with teeth. The teeth are SC-024(b) (the summed w1 rise at level 1
// and at kDefaultLevel, ProfundumAnchor_LevelCalibration) and T002's static API / layer grep
// (check-footprint.js): the Anchor API takes no core and no input buffer. Phase 9 re-runs this on the voice.

TEST_CASE("ProfundumAnchor_NoCompensation", "[systems][profundum][anchor-chain]") {
    constexpr double kFs = 48000.0;
    constexpr std::size_t kBlock = 512;
    const std::size_t total = alignUp(static_cast<std::size_t>(std::llround(2.0 * kFs)), kBlock);
    const float f0 = PT::anchorMidiHz(28);   // E1

    const auto makeCore = [&] {
        auto c = std::make_unique<ProfundumCore>();
        c->prepare(kFs);
        c->setControls(SpectralShapeRecipe::kDefaultControls);
        c->setRetriggerPhase(CRP::Reset);
        c->noteOn(f0);
        return c;
    };

    // The core alone.
    auto alone = makeCore();
    std::vector<float> aloneL(total, 0.0f);
    std::vector<float> aloneR(total, 0.0f);
    for (std::size_t p = 0; p < total; p += kBlock)
        alone->processBlock(aloneL.data() + p, aloneR.data() + p, std::min(kBlock, total - p));

    // The core with an Anchor processed beside it, interleaved block by block (a1 fed per host block).
    auto core = makeCore();
    auto anchor = std::make_unique<AnchorOscillator>();
    anchor->prepare(kFs);
    anchor->setRetriggerPhase(ARP::Reset);
    anchor->noteOn(f0);
    std::vector<float> coreL(total, 0.0f);
    std::vector<float> coreR(total, 0.0f);
    std::vector<float> anchorL(total, 0.0f);
    std::vector<float> anchorR(total, 0.0f);
    for (std::size_t p = 0; p < total; p += kBlock) {
        const std::size_t n = std::min(kBlock, total - p);
        core->processBlock(coreL.data() + p, coreR.data() + p, n);
        anchor->setCoreH1Amplitude(core->deliveredGains()[0] * SpectralShapeRecipe::kCenterPanGain);
        anchor->processBlock(anchorL.data() + p, anchorR.data() + p, n);
    }
    REQUIRE(std::any_of(anchorL.begin(), anchorL.end(), [](float x) { return x != 0.0f; }));   // not vacuous

    CHECK(std::memcmp(aloneL.data(), coreL.data(), total * sizeof(float)) == 0);
    CHECK(std::memcmp(aloneR.data(), coreR.data(), total * sizeof(float)) == 0);

    std::vector<float> summed(total, 0.0f);
    for (std::size_t i = 0; i < total; ++i)
        summed[i] = coreL[i] + anchorL[i];
    std::size_t mismatches = 0;
    for (std::size_t i = 0; i < total; ++i) {
        const float plainAdd = coreL[i] + anchorL[i];
        if (std::bit_cast<std::uint32_t>(summed[i]) != std::bit_cast<std::uint32_t>(plainAdd))
            ++mismatches;
    }
    CHECK(mismatches == 0);
}

// =============================================================================
// T018 chain gates: SC-008(r), SC-009, SC-022 (FR-047, FR-052, FR-067)
// =============================================================================
// Chain = core (setControls(state)) -> setExcitation -> HarmonicFold (prepare(fs, 4096), setAmount(a),
// setFundamentalHz(f0)). Steady state from t_settle = max(1.0 s, 8·tau_release), tau_release =
// kDetectorReleasePeriods / f0 from the Fold's detector at the test note (Phase 2 tasks rule 9: E0 1.55 s);
// then the descriptor rule's 262144 settle samples, then one 262144-sample frame. Every render asserts
// requireLREqual.

namespace {

constexpr double kChainFs = 48000.0;
constexpr std::size_t kChainBlock = 512;
constexpr std::size_t kChainFrame = Krate::DSP::TestUtils::kLowFrequencyFftSize;
constexpr ProfundumCore::ExcitationControls kMaxExcitation{.mix = 1.0f, .drive = 1.0f, .asymmetry = 1.0f};
constexpr ProfundumCore::ExcitationControls kNoExcitation = ProfundumCore::kDefaultExcitation;   // mix 0
static_assert(kNoExcitation.mix == 0.0f, "SC-009 / SC-022 reference: excitation off");

constexpr std::array<AnchorOscillator::Waveform, 5> kAllWaveforms{
    AnchorOscillator::Waveform::Sine, AnchorOscillator::Waveform::Triangle, AnchorOscillator::Waveform::RoundedSine,
    AnchorOscillator::Waveform::AsymmetricSine, AnchorOscillator::Waveform::OctaveDownSine};

const char* waveformName(AnchorOscillator::Waveform w) {
    switch (w) {
        case AnchorOscillator::Waveform::Sine:
            return "sine";
        case AnchorOscillator::Waveform::Triangle:
            return "triangle";
        case AnchorOscillator::Waveform::RoundedSine:
            return "rounded";
        case AnchorOscillator::Waveform::AsymmetricSine:
            return "asymmetric";
        case AnchorOscillator::Waveform::OctaveDownSine:
            return "octave-down";
    }
    return "?";
}

/// t_settle = max(1.0 s, 8·tau_release), tau_release = HarmonicFold::kDetectorReleasePeriods / f0.
double foldReleaseSettleSeconds(double f0) {
    const double tauRelease = static_cast<double>(HarmonicFold::kDetectorReleasePeriods) / f0;
    return std::max(1.0, 8.0 * tauRelease);
}

/// First sample of the measured frame: t_settle, then 262144 settle samples.
std::size_t chainFrameStart(double f0, double fs) {
    return static_cast<std::size_t>(std::ceil(foldReleaseSettleSeconds(f0) * fs)) + kChainFrame;
}

struct Stereo {
    std::vector<float> L, R;
};

struct CoreFedRender {
    Stereo out;
    std::vector<float> a1;   // deliveredGains()[0] · kCenterPanGain after each host block (plan S1 C-2 option (a))
};

/// The core (controls c, excitation e) with a Reset noteOn(f0) at sample 0, rendered in kChainBlock
/// host blocks; records a1 after every block. Asserts L == R.
CoreFedRender renderCoreFed(const SpectralShapeRecipe::Controls& c, const ProfundumCore::ExcitationControls& e,
                            float f0, double fs, std::size_t total) {
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(fs);
    core->setControls(c);
    core->setExcitation(e);
    core->setRetriggerPhase(CRP::Reset);
    core->noteOn(f0);
    CoreFedRender r;
    r.out.L.assign(total, 0.0f);
    r.out.R.assign(total, 0.0f);
    r.a1.reserve(total / kChainBlock + 1);
    for (std::size_t p = 0; p < total; p += kChainBlock) {
        const std::size_t n = std::min(kChainBlock, total - p);
        core->processBlock(r.out.L.data() + p, r.out.R.data() + p, n);
        r.a1.push_back(core->deliveredGains()[0] * SpectralShapeRecipe::kCenterPanGain);
    }
    PT::requireLREqual(r.out.L.data(), r.out.R.data(), total);
    return r;
}

/// The Anchor (waveform w, LOCK, level 1) with a Reset noteOn(f0) at sample 0, in the same host blocks
/// as renderCoreFed, fed a1[k] by setCoreH1Amplitude before block k. Asserts L == R.
Stereo renderAnchorFed(AnchorOscillator::Waveform w, float f0, double fs, std::size_t total,
                       const std::vector<float>& a1) {
    REQUIRE(a1.size() == (total + kChainBlock - 1) / kChainBlock);
    auto anchor = std::make_unique<AnchorOscillator>();
    anchor->prepare(fs);
    anchor->setControls(AnchorOscillator::Controls{.waveform = w, .mode = AnchorOscillator::Mode::Lock, .level = 1.0f});
    anchor->setRetriggerPhase(ARP::Reset);
    anchor->noteOn(f0);
    Stereo s;
    s.L.assign(total, 0.0f);
    s.R.assign(total, 0.0f);
    std::size_t k = 0;
    for (std::size_t p = 0; p < total; p += kChainBlock, ++k) {
        const std::size_t n = std::min(kChainBlock, total - p);
        anchor->setCoreH1Amplitude(a1[k]);
        anchor->processBlock(s.L.data() + p, s.R.data() + p, n);
    }
    PT::requireLREqual(s.L.data(), s.R.data(), total);
    REQUIRE(anchor->stateFinite());
    return s;
}

/// HarmonicFold at `amount` (setFundamentalHz(f0)) over copies of L / R in kChainBlock host blocks.
/// Asserts L == R on the output.
Stereo foldStereo(const std::vector<float>& L, const std::vector<float>& R, float amount, float f0, double fs) {
    REQUIRE(L.size() == R.size());
    auto fold = std::make_unique<HarmonicFold>();
    fold->prepare(fs, 4096);
    fold->setAmount(amount);
    fold->setFundamentalHz(f0);
    Stereo y{L, R};
    const std::size_t total = y.L.size();
    for (std::size_t p = 0; p < total; p += kChainBlock) {
        const std::size_t n = std::min(kChainBlock, total - p);
        fold->processBlock(y.L.data() + p, y.R.data() + p, n);
    }
    PT::requireLREqual(y.L.data(), y.R.data(), total);
    return y;
}

/// a + g·b per channel. Asserts L == R on the result.
Stereo addScaled(const Stereo& a, const Stereo& b, float g) {
    REQUIRE(a.L.size() == b.L.size());
    Stereo s{a.L, a.R};
    for (std::size_t i = 0; i < s.L.size(); ++i) {
        s.L[i] += g * b.L[i];
        s.R[i] += g * b.R[i];
    }
    PT::requireLREqual(s.L.data(), s.R.data(), s.L.size());
    return s;
}

/// E_sub: w1 at fa of one frame starting at `start`.
double frameW1(const std::vector<float>& x, std::size_t start, double fs, double fa) {
    REQUIRE(start + kChainFrame <= x.size());
    return PT::renderedHarmonicPowers(x.data() + start, fs, fa, 1)[0];
}

}  // namespace

// ---- SC-008(r): r == 1 reference on the heavily folded voice ----
// FR-047 evidence: an offline harmonic-sum perceived-fundamental estimate of core -> max excitation ->
// HarmonicFold 100 equals f0 within 1 cent, i.e. r == 1 is correct for the core, excitation and Fold, so
// FOLLOW on this voice correctly renders LOCK's output. This is NOT a tracking test: the Anchor has no
// audio input, and the tracking clause moves to Phase 9 with the first pitch-moving stage (FR-066, SC-021).

TEST_CASE("ProfundumAnchor_FollowReferenceOnFoldedVoice", "[systems][profundum][anchor-chain]") {
    struct State {
        const char* name;
        const SpectralShapeRecipe::Controls* controls;
    };
    const std::array<State, 2> states{{
        {"kSawAnchor", &SpectralShapeRecipe::kSawAnchor},
        {"kGrowl", &SpectralShapeRecipe::kGrowl},
    }};
    for (const State& st : states) {
        for (const int midi : {16, 28, 40, 48}) {   // E0, E1, E2, C3
            const float f0 = PT::anchorMidiHz(midi);
            const auto f0d = static_cast<double>(f0);
            const std::size_t start = chainFrameStart(f0d, kChainFs);
            const std::size_t total = start + kChainFrame;
            const CoreFedRender core = renderCoreFed(*st.controls, kMaxExcitation, f0, kChainFs, total);
            const Stereo folded = foldStereo(core.out.L, core.out.R, 100.0f, f0, kChainFs);
            const double fHat = PT::anchorShsF0(folded.L.data() + start, kChainFs, f0d);
            const double cents = 1200.0 * std::log2(fHat / f0d);
            std::printf("SC-008(r) %-10s midi %d f0 %.4f Hz: folded-voice SHS f0 %.6f Hz, %+.4f cent (bar 1)\n",
                        st.name, midi, f0d, fHat, cents);
            INFO(st.name << " midi " << midi);
            CHECK(std::abs(cents) <= 1.0);
        }
    }
}

// ---- SC-009: the sub preserved through maximum Fold (FR-052) ----
// S = Fold100(excite(1, 1, 1)(core)) + Anchor; R = core (mix 0, Fold not inserted) + Anchor. One Anchor
// render per waveform, fed the unprocessed (R) core's a1, is added to both chains: the same a1 and the
// same calls in S and R, so only the core path differs.
// Gates re-ruled 2026-10-12 (spec Clarifications "Session 2026-10-12 — chain-gate re-rulings"): per gated
// row, (i) coherence: |dDb - pred| <= 0.1 dB, pred = 20*log10(|1 + rho*e^{j*theta}| / 2) with rho the core-only
// h1 amplitude ratio (folded / reference core) and theta the S-chain core h1 phase re the Anchor (rad);
// (ii) bound: |dDb| <= 4.1 dB, Phase 2's ratified -12 dB h1 clause at level 1 (20*log10(1.251/2) = -4.08 dB).

constexpr double kSubCoherenceBarDb = 0.1;   // SC-009 (i)
constexpr double kSubBoundBarDb = 4.1;       // SC-009 (ii)

TEST_CASE("ProfundumAnchor_SubPreservedThroughMaxFold", "[systems][profundum][anchor-chain]") {
    const SpectralShapeRecipe::Controls mid = PT::midGrid();
    struct State {
        const char* name;
        const SpectralShapeRecipe::Controls* controls;
    };
    const std::array<State, 2> states{{
        {"mid grid", &mid},
        {"kSawAnchor", &SpectralShapeRecipe::kSawAnchor},
    }};
    for (const State& st : states) {
        for (const int midi : {16, 28, 40}) {   // E0, E1, E2
            const float f0 = PT::anchorMidiHz(midi);
            const auto f0d = static_cast<double>(f0);
            const std::size_t start = chainFrameStart(f0d, kChainFs);
            const std::size_t total = start + kChainFrame;
            const CoreFedRender ref = renderCoreFed(*st.controls, kNoExcitation, f0, kChainFs, total);
            const CoreFedRender proc = renderCoreFed(*st.controls, kMaxExcitation, f0, kChainFs, total);
            const Stereo folded = foldStereo(proc.out.L, proc.out.R, 100.0f, f0, kChainFs);
            const double w1Fold = frameW1(folded.L, start, kChainFs, f0d);
            const double w1Ref = frameW1(ref.out.L, start, kChainFs, f0d);
            REQUIRE(w1Ref > 0.0);
            const double coreChangeDb = PT::safeDb10(w1Fold / w1Ref);
            const double rho = std::sqrt(w1Fold / w1Ref);   // core-only h1 amplitude ratio
            const std::size_t phaseLen = lockWindowLength(kChainFs, f0d);
            REQUIRE(start + phaseLen <= total);
            const double thFold = PT::goertzelPhase(folded.L.data() + start, phaseLen, kChainFs, f0d);
            const double thRef = PT::goertzelPhase(ref.out.L.data() + start, phaseLen, kChainFs, f0d);

            for (const AnchorOscillator::Waveform w : kAllWaveforms) {
                const bool octave = w == AnchorOscillator::Waveform::OctaveDownSine;
                const double fa = octave ? 0.5 * f0d : f0d;
                const Stereo a = renderAnchorFed(w, f0, kChainFs, total, ref.a1);
                const Stereo s = addScaled(folded, a, 1.0f);
                const Stereo r = addScaled(ref.out, a, 1.0f);
                const double eS = frameW1(s.L, start, kChainFs, fa);
                const double eR = frameW1(r.L, start, kChainFs, fa);
                REQUIRE(eR > 0.0);
                const double dDb = PT::safeDb10(eS / eR);
                if (octave) {
                    std::printf("SC-009 %-10s midi %d %-11s (recorded, not gated): E_sub(S) - E_sub(R) at f0/2 "
                                "%+.4f dB; core-only w1 change at f0 %+.4f dB\n",
                                st.name, midi, waveformName(w), dDb, coreChangeDb);
                    continue;
                }
                const double thA = PT::goertzelPhase(a.L.data() + start, phaseLen, kChainFs, f0d);
                const double phS = wrapDeg((thFold - thA) * kRadToDeg);
                const double phR = wrapDeg((thRef - thA) * kRadToDeg);
                const double theta = thFold - thA;   // radians
                const double pred = 20.0 * std::log10(
                                               std::hypot(1.0 + rho * std::cos(theta), rho * std::sin(theta)) / 2.0);
                std::printf("SC-009 %-10s midi %d %-11s: E_sub(S) - E_sub(R) %+.4f dB, coherent-sum pred %+.4f dB, "
                            "residual %+.4f dB (bars: residual 0.1, |sum| 4.1); core-only w1 change %+.4f dB; "
                            "core h1 phase re Anchor: S %+.3f deg, R %+.3f deg\n",
                            st.name, midi, waveformName(w), dDb, pred, dDb - pred, coreChangeDb, phS, phR);
                INFO(st.name << " midi " << midi << " " << waveformName(w) << " dDb " << dDb << " pred " << pred);
                CHECK(std::abs(dDb - pred) <= kSubCoherenceBarDb);
                CHECK(std::abs(dDb) <= kSubBoundBarDb);
            }
        }
    }
}

// ---- SC-022: the pre-Fold feed (FR-067) ----
// The Anchor (sine, LOCK, level 1, a1 from the core) is rendered ONCE into A; out = HarmonicFold(core +
// g·A) + A for g in {0, 1}, Fold in {65, 100}, at E1 and C2. Core: mid grid, excitation off.

namespace {

struct FoldFeedCell {
    int midi = 0;
    float amount = 0.0f;
    std::array<double, 2> thdDb{};        // sum_{n>=2} w_n / w1 (dB), per g
    std::array<double, 2> centroidOct{};  // describeRendered centroid, per g
    std::array<double, 2> w1{};           // E_sub at f0, per g
    std::array<bool, 2> lrEqual{};        // memcmp(L, R) == 0, per g
};

std::vector<FoldFeedCell> renderFoldFeed() {
    std::vector<FoldFeedCell> cells;
    const SpectralShapeRecipe::Controls mid = PT::midGrid();
    for (const int midi : {28, 36}) {   // E1, C2
        const float f0 = PT::anchorMidiHz(midi);
        const auto f0d = static_cast<double>(f0);
        const std::size_t start = chainFrameStart(f0d, kChainFs);
        const std::size_t total = start + kChainFrame;
        const CoreFedRender core = renderCoreFed(mid, kNoExcitation, f0, kChainFs, total);
        const Stereo a = renderAnchorFed(AnchorOscillator::Waveform::Sine, f0, kChainFs, total, core.a1);
        const int harmonics = static_cast<int>(std::floor(0.5 * kChainFs / f0d));
        for (const float amount : {65.0f, 100.0f}) {
            FoldFeedCell cell;
            cell.midi = midi;
            cell.amount = amount;
            for (std::size_t g = 0; g < 2; ++g) {
                const Stereo in = addScaled(core.out, a, static_cast<float>(g));
                const Stereo folded = foldStereo(in.L, in.R, amount, f0, kChainFs);
                const Stereo out = addScaled(folded, a, 1.0f);
                cell.lrEqual[g] = std::memcmp(out.L.data(), out.R.data(), total * sizeof(float)) == 0;
                const auto d = PT::describeRendered(out.L.data() + start, kChainFs, f0d, harmonics);
                REQUIRE(d.eSub > 0.0);
                cell.thdDb[g] = PT::safeDb10((d.eTotal - d.eSub) / d.eSub);
                cell.centroidOct[g] = d.centroidOct;
                cell.w1[g] = d.eSub;
            }
            cells.push_back(cell);
        }
    }
    return cells;
}

}  // namespace

TEST_CASE("ProfundumAnchor_FoldFeedAudible", "[systems][profundum][anchor-chain]") {
    const std::vector<FoldFeedCell> cells = renderFoldFeed();
    REQUIRE(cells.size() == 4);
    bool pass = true;
    for (const FoldFeedCell& c : cells) {
        const double dThd = c.thdDb[1] - c.thdDb[0];
        const double dCentroid = c.centroidOct[1] - c.centroidOct[0];
        const bool audible = std::abs(dThd) >= 3.0 || std::abs(dCentroid) >= 0.25;
        std::printf("SC-022(1) midi %d Fold %.0f: THD feed 0 %+.3f dB, feed 1 %+.3f dB, delta %+.3f dB (bar 3); "
                    "centroid feed 0 %.4f oct, feed 1 %.4f oct, delta %+.4f oct (bar 0.25): %s\n",
                    c.midi, static_cast<double>(c.amount), c.thdDb[0], c.thdDb[1], dThd, c.centroidOct[0],
                    c.centroidOct[1], dCentroid, audible ? "audible" : "NOT audible");
        INFO("midi " << c.midi << " Fold " << c.amount);
        CHECK(audible);   // a recorded outcome (feed not shipped on FAIL), not a build break (plan SC-022)
        pass = pass && audible;
    }
    std::printf("SC-022 VERDICT: %s (feed %s; decides T021's branch)\n", pass ? "PASS" : "FAIL",
                pass ? "ships as anchorFoldFeed" : "not shipped");
}

constexpr double kFeedSubFloorDb = -1.0;    // SC-022(2) floor
constexpr double kFeedSubCeilingDb = 6.27;  // SC-022(2) ceiling

TEST_CASE("ProfundumAnchor_FoldFeedSubPreserved", "[systems][profundum][anchor-chain]") {
    const std::vector<FoldFeedCell> cells = renderFoldFeed();
    REQUIRE(cells.size() == 4);
    for (const FoldFeedCell& c : cells) {
        REQUIRE(c.w1[0] > 0.0);
        const double dDb = PT::safeDb10(c.w1[1] / c.w1[0]);
        std::printf("SC-022(2) midi %d Fold %.0f: E_sub(feed 1) - E_sub(feed 0) %+.4f dB (floor -1 dB, ceiling "
                    "+6.27 dB)\n",
                    c.midi, static_cast<double>(c.amount), dDb);
        INFO("midi " << c.midi << " Fold " << c.amount << " dDb " << dDb);
        // Re-ruled 2026-10-12 (spec Clarifications "Session 2026-10-12"): one-sided floor (the sub is never lost)
        // plus a coherent ceiling (6.02 dB doubling + 0.25 dB: the Fold cannot add more h1 than the Anchor's copy).
        REQUIRE(dDb >= kFeedSubFloorDb);
        REQUIRE(dDb <= kFeedSubCeilingDb);
    }
}

TEST_CASE("ProfundumAnchor_FoldFeedMono", "[systems][profundum][anchor-chain]") {
    const std::vector<FoldFeedCell> cells = renderFoldFeed();
    REQUIRE(cells.size() == 4);
    for (const FoldFeedCell& c : cells) {
        std::printf("SC-022(3) midi %d Fold %.0f: L == R bitwise feed 0 %s, feed 1 %s\n", c.midi,
                    static_cast<double>(c.amount), c.lrEqual[0] ? "yes" : "NO", c.lrEqual[1] ? "yes" : "NO");
        INFO("midi " << c.midi << " Fold " << c.amount);
        REQUIRE(c.lrEqual[0]);
        REQUIRE(c.lrEqual[1]);
    }
}
