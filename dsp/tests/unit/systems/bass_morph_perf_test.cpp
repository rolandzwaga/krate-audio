// ==============================================================================
// Layer 3: System Tests - BassMorph perf TU (shipping FP mode, not -fno-fast-math)
//                          (Profundum Phase 8, specs/profundum-phase8-bass-morph)
// ==============================================================================
// tasks.md T014 (SC-018, FR-083; plan C-15 D-10). Both cases are [performance][.perf]: hidden, never inside a gate,
// run by the main loop through evidence/close/cpu-lane.sh, last and alone, pinned to the performance cores.
//
//   BassMorph_Cpu                          SC-018: advanceInterval() on the T008 workload (seed 0x8018, every leaf
//                                          differing, no governing amount 0, 2000 ms ramp re-targeted 0 <-> 1 on
//                                          landing); 10^6 calls per repetition, median of 3, CHECK <= 333.3 ns per
//                                          interval (0.05 % of 666 667 ns at 48 kHz). Last line
//                                          "SC018 VERDICT <GREEN|RED> <ns>"; writes evidence/sc018-cpu.txt.
//   BassMorph_WorstTransitionIntervalRecord FR-083 recorded handoff (not gated): renderMorphPoly at E1, A1, E2, C3,
//                                          a pair whose rendered discrete groups (Character, ImageMode, FieldMotion,
//                                          Anchor waveform and mode) all switch at one interval (rule 3, every
//                                          governing amount non-zero, Blend so p_a = p). Per-interval wall time of
//                                          the whole chain; the worst interval's % of one core printed against
//                                          Phase 5's 2.65 % crossfade cell; writes evidence/worst-interval.txt.
//
// Per-interval timing (recorded decision): the fixture is read-only after T007, so the clock is read in its one
// per-interval hook, cfg.tail.perInterval (bass_morph_test_helpers.h step 9, called once per grid point). The
// delta between consecutive hook calls is one whole interval cycle of the chain (steps 9-11, the 32-sample voice
// blocks, the shared tail, then steps 1-8 of the next interval). It includes the fixture's own overhead (the
// linear twin core per voice that only D_nl reads, and the tap writes), so the record is an upper bound on the
// shipping chain. Three renders; the per-interval median across them; the worst of those medians is the record
// (a single preemption spike in one render does not set it).
// FTZ / DAZ: set by the render record (it runs the core and the character stage); the engine-only SC-018 case
// never sets it (tasks.md rule 9, plan R-7), as BassMorph_CpuEarlyProbe.
// ==============================================================================

#include "bass_morph_test_helpers.h"

#include <catch2/catch_test_macros.hpp>

#include <enable_ftz_daz.h>

#include <krate/dsp/systems/bass_morph_engine.h>
#include <krate/dsp/systems/morph_state.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <iostream>
#include <random>
#include <span>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace Bmt = Krate::DSP::BassMorphTest;
using Krate::DSP::AnchorOscillator;
using Krate::DSP::BassCharacterStage;
using Krate::DSP::BassMorphEngine;
using Krate::DSP::BassStereoField;
using Krate::DSP::MorphState;
using BmcGroup = MorphState::SelectionGroup;

constexpr double kBmcFs = 48000.0;
constexpr double kBmcIntervalNs = 1e9 * 32.0 / kBmcFs;   // 666 667 ns at 48 kHz
constexpr double kBmcBarNs = 333.3;                       // plan C-15 D-10: 0.05 % of one core
constexpr double kBmcPhase5CrossfadePct = 2.65;           // Phase 5's crossfade cell (spec FR-083, plan S12)

/// tasks.md rule 11: evidence goes under specs/profundum-phase8-bass-morph/evidence/.
std::string bmcEvidenceDir() {
    return std::string(KRATE_DSP_TESTS_DIR) + "/../../specs/profundum-phase8-bass-morph/evidence/";
}

void bmcWriteEvidence(const std::string& name, const std::string& text) {
    const std::string dir = bmcEvidenceDir();
    std::filesystem::create_directories(dir);
    std::ofstream out(dir + name);
    out << text;
}

bool bmcEveryLeafDiffers(const MorphState& a, const MorphState& b) {
    return Bmt::differingLeaves(a, b).size() == Krate::DSP::kMorphLeafFieldCount;
}

bool bmcNoGoverningZero(const MorphState& s) {
    return s.characterAmount != 0.0f && s.field.spread != 0.0f && s.field.motionDepth != 0.0f
           && s.motion.amount != 0.0f && s.anchor.level != 0.0f;
}

template <std::size_t N>
double bmcMedian(std::array<double, N> v) {
    std::sort(v.begin(), v.end());
    return v[N / 2];
}

/// FR-083 pair: the showcase pair with every governing amount non-zero on both sides (rule 3, so each group switches
/// at kThetaUp, not at the first interval) and every rendered discrete group differing: Character Tube -> Fracture,
/// ImageMode Solid -> Wide, FieldMotion Drift / unsynced -> Cycle / synced, Anchor Sine / Lock -> Triangle / Punch.
std::pair<MorphState, MorphState> bmcAllGroupsPair() {
    auto [a, b] = Bmt::showcasePair();
    a.characterAlgorithm = BassCharacterStage::Algorithm::Tube;
    a.characterAmount = 0.5f;
    b.characterAlgorithm = BassCharacterStage::Algorithm::Fracture;
    b.characterAmount = 0.8f;
    a.field.mode = BassStereoField::ImageMode::Solid;
    a.field.spread = 0.5f;
    b.field.mode = BassStereoField::ImageMode::Wide;
    b.field.spread = 1.0f;
    a.field.motionDepth = 0.5f;
    a.field.motionSource = BassStereoField::MotionSource::Drift;
    a.field.cycleSync = false;
    b.field.motionDepth = 1.0f;
    b.field.motionSource = BassStereoField::MotionSource::Cycle;
    b.field.cycleSync = true;
    a.anchor.waveform = AnchorOscillator::Waveform::Sine;
    a.anchor.mode = AnchorOscillator::Mode::Lock;
    a.anchor.level = 1.0f;
    b.anchor.waveform = AnchorOscillator::Waveform::Triangle;
    b.anchor.mode = AnchorOscillator::Mode::Punch;
    b.anchor.level = 0.5f;
    REQUIRE(Krate::DSP::isValidMorphState(a));
    REQUIRE(Krate::DSP::isValidMorphState(b));
    return {a, b};
}

}  // namespace

// -----------------------------------------------------------------------------
// SC-018: advanceInterval() CPU (plan C-15 D-10)
// -----------------------------------------------------------------------------

TEST_CASE("BassMorph_Cpu", "[systems][profundum][bass-morph][sc-018][performance][.perf]") {
    constexpr long kMaxDraws = 10'000'000;
    constexpr std::size_t kCalls = 1'000'000;
    constexpr std::size_t kWarmupCalls = 200'000;
    constexpr int kReps = 3;

    // The T008 workload: seed 0x8018, redrawn until every leaf differs and no governing amount is 0.
    std::mt19937 rng(0x8018u);
    MorphState a{};
    MorphState b{};
    long draws = 0;
    bool found = false;
    while (draws < kMaxDraws && !found) {
        const auto pr = Bmt::randomValidPair(rng, 0.0f);
        ++draws;
        if (bmcNoGoverningZero(pr.first) && bmcNoGoverningZero(pr.second) && bmcEveryLeafDiffers(pr.first, pr.second)) {
            a = pr.first;
            b = pr.second;
            found = true;
        }
    }
    INFO("draws " << draws);
    REQUIRE(found);

    BassMorphEngine e;
    REQUIRE(e.setEndpoint(BassMorphEngine::Endpoint::A, a));
    REQUIRE(e.setEndpoint(BassMorphEngine::Endpoint::B, b));
    e.setPositionRampMs(2000.0f);
    e.setPosition(0.0f);
    e.prepare(kBmcFs);
    e.setPosition(1.0f);

    // Re-target 0 <-> 1 on landing (p at its target bitwise and p_a at c_m(p) bitwise), so every interval moves.
    std::size_t retargets = 0;
    auto step = [&e, &retargets]() -> float {
        const BassMorphEngine::Resolved& r = e.advanceInterval();
        const float pos = e.position();
        if (std::bit_cast<std::uint32_t>(pos) == std::bit_cast<std::uint32_t>(e.targetPosition())
            && std::bit_cast<std::uint32_t>(e.anchorPosition())
                   == std::bit_cast<std::uint32_t>(BassMorphEngine::curve(e.anchorMorphMode(), pos))) {
            e.setPosition(e.targetPosition() >= 1.0f ? 0.0f : 1.0f);
            ++retargets;
        }
        return r.state.characterAmount + r.position;
    };

    // Untimed warm-up; counts intervals whose ramped p did not move (recorded, not gated).
    volatile float sink = 0.0f;
    std::size_t staticIntervals = 0;
    float prevPos = e.resolved().position;
    for (std::size_t i = 0; i < kWarmupCalls; ++i) {
        sink = sink + step();
        const float pos = e.resolved().position;
        if (std::bit_cast<std::uint32_t>(pos) == std::bit_cast<std::uint32_t>(prevPos))
            ++staticIntervals;
        prevPos = pos;
    }

    std::array<double, kReps> ns{};
    for (int rep = 0; rep < kReps; ++rep) {
        float acc = 0.0f;
        const auto t0 = std::chrono::steady_clock::now();
        for (std::size_t i = 0; i < kCalls; ++i)
            acc += step();
        const auto t1 = std::chrono::steady_clock::now();
        sink = sink + acc;
        ns[static_cast<std::size_t>(rep)] =
            static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count())
            / static_cast<double>(kCalls);
    }
    const double median = bmcMedian(ns);
    const double pct = median / kBmcIntervalNs * 100.0;
    const bool pass = median <= kBmcBarNs;

    std::ostringstream os;
    os << std::setprecision(6);
    os << "SC-018 advanceInterval CPU (tasks.md T014; plan C-15 D-10). Run alone, pinned (evidence/close/cpu-lane.sh).\n";
    os << "workload: seed 0x8018, randomValidPair(rng, 0) redrawn until every leaf differs and no governing amount "
          "is 0; draws "
       << draws << "; fs " << kBmcFs << " Hz; ramp 2000 ms; anchor morph mode Blend (default); target re-set 0 <-> 1 "
       << "on landing\n";
    os << "calls per repetition " << kCalls << ", repetitions " << kReps << ", warm-up " << kWarmupCalls
       << " calls (static intervals " << staticIntervals << "), re-targets over the run " << retargets << "\n";
    os << "repetitions_ns_per_interval";
    for (const double v : ns)
        os << ' ' << v;
    os << '\n';
    os << "ns_per_interval " << median << '\n';
    os << "% of one core " << pct << " (bar 0.05 %)\n";
    os << "SC018 VERDICT " << (pass ? "GREEN" : "RED") << ' ' << median << '\n';
    std::cout << os.str();
    bmcWriteEvidence("sc018-cpu.txt", os.str());
    static_cast<void>(sink);

    INFO("ns_per_interval " << median << " vs bar " << kBmcBarNs);
    CHECK(median <= kBmcBarNs);
}

// -----------------------------------------------------------------------------
// FR-083: the worst simultaneous-transition interval (recorded Phase 9 handoff, not gated)
// -----------------------------------------------------------------------------

TEST_CASE("BassMorph_WorstTransitionIntervalRecord", "[systems][profundum][bass-morph][fr-083][performance][.perf]") {
    enableFTZDAZ();
    constexpr int kReps = 3;
    constexpr double kSeconds = 1.5;
    constexpr std::size_t kSwitchSample = 24'000;   // 0.5 s, a multiple of 32: the position target goes 0 -> 1

    const auto [a, b] = bmcAllGroupsPair();
    for (const BmcGroup g : {BmcGroup::Character, BmcGroup::ImageMode, BmcGroup::FieldMotion, BmcGroup::Anchor}) {
        // rule 3: both governing amounts non-zero on every rendered group
        const auto governing = [g](const MorphState& s) {
            switch (g) {
            case BmcGroup::Character:
                return s.characterAmount;
            case BmcGroup::ImageMode:
                return s.field.spread;
            case BmcGroup::FieldMotion:
                return s.field.motionDepth;
            default:
                return s.anchor.level;
            }
        };
        const float ga = governing(a);
        const float gb = governing(b);
        REQUIRE(ga != 0.0f);
        REQUIRE(gb != 0.0f);
    }

    const std::array<const char*, 4> noteNames{"E1", "A1", "E2", "C3"};
    const std::array<double, 4> f0s{static_cast<double>(Krate::DSP::ProfundumTest::anchorMidiHz(28)),
                                    static_cast<double>(Krate::DSP::ProfundumTest::anchorMidiHz(33)),
                                    static_cast<double>(Krate::DSP::ProfundumTest::anchorMidiHz(40)),
                                    static_cast<double>(Krate::DSP::ProfundumTest::anchorMidiHz(48))};

    Bmt::MorphChainConfig cfg;
    cfg.a = a;
    cfg.b = b;
    cfg.mode = BassMorphEngine::AnchorMorphMode::Blend;   // p_a = p, so the Anchor group switches with the others
    cfg.initialPosition = 0.0f;
    cfg.rampMs = BassMorphEngine::kDefaultPositionRampMs;
    cfg.fs = kBmcFs;
    cfg.seconds = kSeconds;
    cfg.noteOnSample = 0;
    cfg.ampEnvelope = false;   // steady: the four voices sound through the switch
    Bmt::MorphEvent ev;
    ev.sample = kSwitchSample;
    ev.kind = Bmt::MorphEvent::Kind::Position;
    ev.value = 1.0f;
    cfg.automation.push_back(ev);

    const std::size_t total = Krate::DSP::MassDynamicsTest::renderLength(cfg.fs, cfg.seconds);
    const std::size_t intervals = (total + Bmt::kGrid - 1) / Bmt::kGrid;

    using Clock = std::chrono::steady_clock;
    std::vector<Clock::time_point> stamps;
    std::vector<std::size_t> stampSamples;
    std::array<std::vector<double>, kReps> deltaNs;
    std::vector<Bmt::MorphChainTaps::SetterCall> switches;
    std::vector<BassMorphEngine::Resolved> resolved;

    for (int rep = 0; rep < kReps; ++rep) {
        stamps.clear();
        stampSamples.clear();
        stamps.reserve(intervals + 1);
        stampSamples.reserve(intervals + 1);
        cfg.tail.perInterval = [&stamps, &stampSamples](std::size_t sample, BassStereoField&) {
            stamps.push_back(Clock::now());
            stampSamples.push_back(sample);
        };
        Bmt::MorphChainTaps taps = Bmt::renderMorphPoly(std::span<const double>(f0s.data(), f0s.size()), cfg);
        REQUIRE(stamps.size() == taps.resolvedPerInterval.size());
        REQUIRE(stamps.size() >= 2);
        auto& d = deltaNs[static_cast<std::size_t>(rep)];
        d.resize(stamps.size() - 1);
        for (std::size_t k = 0; k + 1 < stamps.size(); ++k)
            d[k] = static_cast<double>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(stamps[k + 1] - stamps[k]).count());
        if (rep == 0) {
            switches = taps.discreteSetterCalls;
            resolved = taps.resolvedPerInterval;
            const bool finite = std::all_of(taps.stageOutL.begin(), taps.stageOutL.end(),
                                            [](float v) { return Krate::DSP::detail::isFinite(v); });
            REQUIRE(finite);
        }
    }

    // The switch: every rendered discrete group must have issued its setter, all at one grid point (rule 3).
    REQUIRE_FALSE(switches.empty());
    const std::size_t switchSample = switches.front().sample;
    bool sawCharacter = false, sawMode = false, sawMotion = false, sawWave = false, sawAnchorMode = false;
    bool sameInterval = true;
    for (const auto& c : switches) {
        sameInterval = sameInterval && c.sample == switchSample;
        sawCharacter = sawCharacter || c.leaf == Bmt::kLeafCharacterAlgorithm;
        sawMode = sawMode || c.leaf == Bmt::kLeafFieldMode;
        sawMotion = sawMotion || c.leaf == Bmt::kLeafFieldMotionSource || c.leaf == Bmt::kLeafFieldCycleSync;
        sawWave = sawWave || c.leaf == Bmt::kLeafAnchorWaveform;
        sawAnchorMode = sawAnchorMode || c.leaf == Bmt::kLeafAnchorMode;
    }
    CHECK(sawCharacter);
    CHECK(sawMode);
    CHECK(sawMotion);
    CHECK(sawWave);
    CHECK(sawAnchorMode);
    CHECK(sameInterval);
    CHECK(switchSample > kSwitchSample);   // rule 3: at kThetaUp, not at the first moving interval

    // Per-interval median across the renders; the worst of those medians is the record.
    const std::size_t n = deltaNs[0].size();
    std::vector<double> med(n);
    for (std::size_t k = 0; k < n; ++k) {
        std::array<double, kReps> v{};
        for (int rep = 0; rep < kReps; ++rep)
            v[static_cast<std::size_t>(rep)] = deltaNs[static_cast<std::size_t>(rep)][k];
        med[k] = bmcMedian(v);
    }
    // Interval k's delta runs from hook k (sample stampSamples[k]) to hook k + 1.
    std::size_t switchIdx = 0;
    while (switchIdx < n && stampSamples[switchIdx] < switchSample)
        ++switchIdx;
    const std::size_t worstIdx =
        static_cast<std::size_t>(std::distance(med.begin(), std::max_element(med.begin(), med.end())));
    std::vector<double> sorted = med;
    std::sort(sorted.begin(), sorted.end());
    const double typical = sorted[sorted.size() / 2];
    const double worst = med[worstIdx];
    // The switch window: the setter interval and the 10 ms crossfade after it (one hook-to-hook delta each).
    const std::size_t windowLen = static_cast<std::size_t>(0.010 * kBmcFs) / Bmt::kGrid + 2;
    double windowWorst = 0.0;
    std::size_t windowWorstIdx = switchIdx;
    for (std::size_t k = (switchIdx > 0 ? switchIdx - 1 : 0); k < std::min(n, switchIdx + windowLen); ++k) {
        if (med[k] > windowWorst) {
            windowWorst = med[k];
            windowWorstIdx = k;
        }
    }
    auto pctOf = [](double v) { return v / kBmcIntervalNs * 100.0; };

    std::ostringstream os;
    os << std::setprecision(6);
    os << "FR-083 worst simultaneous-transition interval record (tasks.md T014; spec FR-083). Not gated: Phase 9's\n"
          "CPU budget rules on it. Run alone, pinned (evidence/close/cpu-lane.sh).\n";
    os << "chain: renderMorphPoly, 4 voices";
    for (std::size_t v = 0; v < f0s.size(); ++v)
        os << ' ' << noteNames[v] << '=' << f0s[v] << "Hz";
    os << "; fs " << kBmcFs << " Hz; " << kSeconds << " s; steady (ampEnvelope off); host block " << cfg.hostBlock
       << "; anchor morph Blend; ramp " << cfg.rampMs << " ms; position 0 -> 1 at sample " << kSwitchSample << '\n';
    os << "pair: showcasePair() with Character Tube/0.5 -> Fracture/0.8, ImageMode Solid/0.5 -> Wide/1, FieldMotion "
          "Drift/unsynced/0.5 -> Cycle/synced/1, Anchor Sine/Lock/1 -> Triangle/Punch/0.5 (every governing amount "
          "non-zero: rule 3)\n";
    os << "timing: steady_clock at the fixture's per-interval hook (tail.perInterval); one delta = one whole interval "
          "cycle; includes fixture overhead (linear twin core per voice, tap writes), so an upper bound; "
       << kReps << " renders, per-interval median\n";
    os << "discrete setter issues (sample leaf value):";
    for (const auto& c : switches)
        os << " (" << c.sample << ' ' << static_cast<unsigned>(c.leaf) << ' ' << static_cast<unsigned>(c.value) << ')';
    os << "\nall groups at one interval " << (sameInterval ? "yes" : "NO") << "; switch sample " << switchSample
       << " (interval " << switchIdx << "), position there "
       << (switchIdx < resolved.size() ? resolved[switchIdx].position : -1.0f) << '\n';
    os << "intervals " << n << "; median interval " << typical << " ns = " << pctOf(typical) << " % of one core\n";
    os << "switch window worst (interval " << windowWorstIdx << ", sample " << stampSamples[windowWorstIdx] << ") "
       << windowWorst << " ns = " << pctOf(windowWorst) << " % of one core\n";
    os << "render worst (interval " << worstIdx << ", sample " << stampSamples[worstIdx] << ") " << worst
       << " ns = " << pctOf(worst) << " % of one core\n";
    os << "around the switch (interval: median ns):";
    for (std::size_t k = (switchIdx > 2 ? switchIdx - 2 : 0); k < std::min(n, switchIdx + windowLen); ++k)
        os << ' ' << k << ':' << med[k];
    os << '\n';
    os << "FR083 RECORD worst_interval_pct " << pctOf(worst) << " switch_window_pct " << pctOf(windowWorst)
       << " median_pct " << pctOf(typical) << " vs Phase 5 crossfade cell " << kBmcPhase5CrossfadePct << " % (ratio "
       << pctOf(worst) / kBmcPhase5CrossfadePct << ")\n";
    std::cout << os.str();
    bmcWriteEvidence("worst-interval.txt", os.str());
}
