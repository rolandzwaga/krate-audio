// ==============================================================================
// Profundum Phase 9 (specs/profundum-phase9-voice-engine) - profundum_voice_engine_helpers_test.cpp
// ==============================================================================
// Holds: observer / seam defaults (T004, FR-086, plan S8); fixture self-tests (driver, observer, readers,
//        click machinery) are appended by T009.
// Filled by: T004, T009 (each appends).
// Includes every Phase 9 header so each is compiled by every gate from Group 1 on.
// ==============================================================================

#include <krate/dsp/systems/profundum_engine.h>
#include <krate/dsp/systems/profundum_engine_observer.h>
#include <krate/dsp/systems/profundum_macro_matrix.h>
#include <krate/dsp/systems/profundum_motion.h>
#include <krate/dsp/systems/profundum_voice.h>

#include "profundum_voice_engine_test_helpers.h"   // T009

#include <catch2/catch_test_macros.hpp>

#include <enable_ftz_daz.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

using namespace Krate::DSP;

// ------------------------------------------------------------------------------
// T004 - plan S8 / S6.1: seams, call / grid records, observer base (FR-086)
// ------------------------------------------------------------------------------

static_assert(std::is_trivially_copyable_v<ProfundumEngineSeams>);
static_assert(std::is_trivially_copyable_v<ProfundumCallRecord>);
static_assert(std::is_trivially_copyable_v<ProfundumGridRecord>);

TEST_CASE("ProfundumObserver_SeamDefaults", "[systems][profundum][voice-engine][fr-086]") {
    STATIC_REQUIRE(std::is_trivially_copyable_v<ProfundumEngineSeams>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<ProfundumCallRecord>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<ProfundumGridRecord>);

    const ProfundumEngineSeams s{};
    CHECK_FALSE(s.macroBypass);
    CHECK_FALSE(s.anchorDetached);
    CHECK_FALSE(s.forceLinearCore);
    CHECK_FALSE(s.disableStealFade);
    CHECK_FALSE(s.disableDeclick);
    CHECK_FALSE(s.followNoBypass);
    CHECK_FALSE(s.lockReadsCore);
    CHECK_FALSE(s.anchorPhaseTap);

    // Exactly the eight bools of plan S6.1, nothing else.
    STATIC_REQUIRE(sizeof(ProfundumEngineSeams) == 8 * sizeof(bool));
}

namespace {

// The order of plan S8's enum is normative (SC-032 compares call sequences).
constexpr ProfundumCall kVoiceEngineCallOrder[] = {
    ProfundumCall::Events,         ProfundumCall::MorphAdvance,   ProfundumCall::Macros,
    ProfundumCall::Motion,         ProfundumCall::LawSetBase,     ProfundumCall::LawSetMass,
    ProfundumCall::LawAdvance,     ProfundumCall::FieldSetControls, ProfundumCall::FieldAdvance,
    ProfundumCall::DcResync,       ProfundumCall::VoicePan,       ProfundumCall::VoiceWide,
    ProfundumCall::CoreControls,   ProfundumCall::CoreExcitation, ProfundumCall::FoldAmount,
    ProfundumCall::FoldF0,         ProfundumCall::Shape,          ProfundumCall::CharAlgorithm,
    ProfundumCall::CharAmount,     ProfundumCall::CharTone,       ProfundumCall::HalfWave,
    ProfundumCall::FollowRatio,    ProfundumCall::AnchorControls, ProfundumCall::LockBypass,
    ProfundumCall::A1,             ProfundumCall::DropSetters,    ProfundumCall::AmpEnv,
    ProfundumCall::AmpCurve,       ProfundumCall::SyncPhase,      ProfundumCall::NoteOnCore,
    ProfundumCall::NoteOnAnchor,   ProfundumCall::DynDuration,    ProfundumCall::DynCompression,
    ProfundumCall::DynTrim,        ProfundumCall::DynAttack,      ProfundumCall::DynSustain,
    ProfundumCall::DynKey,
};

} // namespace

TEST_CASE("ProfundumObserver_DefaultCallbacksAreNoOps", "[systems][profundum][voice-engine][fr-086]") {
    ProfundumEngineObserver o;

    STATIC_REQUIRE(noexcept(o.onGrid(ProfundumGridRecord{})));
    STATIC_REQUIRE(noexcept(o.onCall(ProfundumCallRecord{})));
    STATIC_REQUIRE(noexcept(o.onVoiceChunk(ProfundumVoiceTaps{})));
    STATIC_REQUIRE(noexcept(o.onEngineChunk(ProfundumEngineTaps{})));

    // The base accepts every callback with a default-constructed record and does nothing.
    o.onGrid(ProfundumGridRecord{});
    o.onCall(ProfundumCallRecord{});
    o.onVoiceChunk(ProfundumVoiceTaps{});
    o.onEngineChunk(ProfundumEngineTaps{});

    // Through the base pointer too (the engine and voice hold a ProfundumEngineObserver*).
    ProfundumEngineObserver* p = &o;
    p->onGrid(ProfundumGridRecord{});
    p->onCall(ProfundumCallRecord{});
    p->onVoiceChunk(ProfundumVoiceTaps{});
    p->onEngineChunk(ProfundumEngineTaps{});
    SUCCEED("every default callback returned");

    // ProfundumCall: 37 enumerators in the S8 order.
    STATIC_REQUIRE(static_cast<int>(ProfundumCall::DynKey) == 36);
    STATIC_REQUIRE(std::is_same_v<std::underlying_type_t<ProfundumCall>, std::uint8_t>);
    constexpr int kCount = static_cast<int>(sizeof(kVoiceEngineCallOrder) / sizeof(kVoiceEngineCallOrder[0]));
    STATIC_REQUIRE(kCount == 37);
    for (int i = 0; i < kCount; ++i) {
        INFO("enumerator index " << i);
        CHECK(static_cast<int>(kVoiceEngineCallOrder[i]) == i);
    }

    // Default-constructed records read zero / null.
    const ProfundumCallRecord c{};
    CHECK(c.sample == 0u);
    CHECK(c.voice == 0u);
    CHECK(c.call == ProfundumCall::Events);
    CHECK_FALSE(c.issued);
    const ProfundumGridRecord g{};
    CHECK(g.resolved == nullptr);
    CHECK(g.afterMacros == nullptr);
    CHECK(g.afterMotion == nullptr);
    const ProfundumVoiceTaps v{};
    CHECK(v.n == 0u);
    CHECK(v.coreL.empty());
    const ProfundumEngineTaps e{};
    CHECK(e.n == 0u);
    CHECK(e.outR.empty());
}

// ------------------------------------------------------------------------------
// T009 - fixture self-tests (plan S10.2, S9.9, S9.11; C-8, C-9; deviation 11)
// ------------------------------------------------------------------------------
// The driver, the observer and the twin through the engine; the readers and the click machinery on known
// signals. TU-local names carry the `ve` / `Ve` prefix inside an anonymous namespace.

namespace {

namespace VE = Krate::DSP::ProfundumTest;

constexpr double kVeFs = 48000.0;
constexpr double kVeE1Hz = 41.20344;
constexpr double kVePi = 3.14159265358979323846;

[[nodiscard]] std::size_t veMismatches(const std::vector<float>& a, const std::vector<float>& b) noexcept {
    if (a.size() != b.size())
        return std::max(a.size(), b.size());
    std::size_t n = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::bit_cast<std::uint32_t>(a[i]) != std::bit_cast<std::uint32_t>(b[i]))
            ++n;
    }
    return n;
}

/// The engine's own Events records with issued == true, by sample.
[[nodiscard]] std::vector<std::uint64_t> veIssuedEventSamples(const VE::VoiceEngineTaps& t) {
    std::vector<std::uint64_t> out;
    for (const ProfundumCallRecord& c : t.calls) {
        if (c.call == ProfundumCall::Events && c.voice == ProfundumEngine::kEngineRecordVoice && c.issued)
            out.push_back(c.sample);
    }
    return out;
}

/// A_s (C-8) / showcase B, MONO, one tapped voice.
[[nodiscard]] VE::VoiceEngineScript veShowcaseScript(std::uint64_t total) {
    const auto pair = BassMorphTest::showcasePair();
    VE::VoiceEngineScript s;
    s.fs = kVeFs;
    s.totalSamples = total;
    s.a = VE::voiceEngineAs(pair.first);
    s.b = pair.second;
    s.tappedVoices = 1;
    return s;
}

/// a sin(w n + phi) + h10 sin(10 (w n + phi)) at kVeFs, in float.
[[nodiscard]] std::vector<float> veSine(std::size_t n, double f0, double a, double h10 = 0.0, double phi = 0.3) {
    std::vector<float> x(n, 0.0f);
    const double w = VE::kTwoPi * f0 / kVeFs;
    for (std::size_t i = 0; i < n; ++i) {
        const double ph = w * static_cast<double>(i) + phi;
        x[i] = static_cast<float>(a * std::sin(ph) + h10 * std::sin(10.0 * ph));
    }
    return x;
}

}  // namespace

TEST_CASE("VoiceEngineHelpers_DriverSplitsAtEvents", "[systems][profundum][voice-engine][fr-063]") {
    enableFTZDAZ();
    VE::VoiceEngineScript s = veShowcaseScript(12000);
    s.events = {
        VE::VoiceEngineEvent{.sample = 5, .kind = VE::VoiceEngineEventKind::NoteOn, .note = VE::kVoiceEngineNoteE1,
                             .velocity = 100},
        VE::VoiceEngineEvent{.sample = 37, .kind = VE::VoiceEngineEventKind::PitchBend, .value = 0.5f},
        VE::VoiceEngineEvent{.sample = 100, .kind = VE::VoiceEngineEventKind::NoteOff, .note = VE::kVoiceEngineNoteE1},
    };

    s.hostBlock = 64;
    const VE::VoiceEngineTaps a = VE::voiceEngineRender(s);
    s.hostBlock = 4096;
    const VE::VoiceEngineTaps b = VE::voiceEngineRender(s);

    // The note sounded (otherwise the bitwise arm compares silence).
    float peak = 0.0f;
    for (const float v : a.outL)
        peak = std::max(peak, std::fabs(v));
    INFO("output peak " << peak);
    CHECK(peak > 1.0e-4f);

    // Output taps (and the pre-dynamics sum) bitwise equal across the host partitions.
    CHECK(veMismatches(a.outL, b.outL) == 0u);
    CHECK(veMismatches(a.outR, b.outR) == 0u);
    CHECK(veMismatches(a.sumL, b.sumL) == 0u);
    CHECK(veMismatches(a.sumR, b.sumR) == 0u);

    // C-9: each event applies at the next grid point.
    const std::vector<std::uint64_t> expected{32, 64, 128};
    CHECK(veIssuedEventSamples(a) == expected);
    CHECK(veIssuedEventSamples(b) == expected);
    CHECK(a.droppedEvents == 0u);
    CHECK(a.grid.size() == (12000u + 31u) / 32u);
}

TEST_CASE("VoiceEngineHelpers_TwinDiffersOnlyInExcitation", "[systems][profundum][voice-engine][fr-086]") {
    enableFTZDAZ();
    VE::VoiceEngineScript s = veShowcaseScript(14400);   // 0.3 s
    s.a.excitation.mix = 1.0f;
    REQUIRE(isValidMorphState(s.a));
    s.events = {VE::VoiceEngineEvent{.sample = 0, .kind = VE::VoiceEngineEventKind::NoteOn,
                                     .note = VE::kVoiceEngineNoteE1, .velocity = 100}};

    const VE::VoiceEngineTaps primary = VE::voiceEngineRenderWithTwin(s);
    const VE::VoiceEngineTaps twin = VE::voiceEngineRender(VE::voiceEngineTwinScript(s));

    // Grid states: the twin may differ from the main render only in excitation.mix (the seam zeroes the handed
    // excitation, not the state), and the main render's state carries mix 1.
    const std::uint8_t mixLeaf = BassMorphTest::leafIndex("excitation.mix");
    REQUIRE(mixLeaf != BassMorphTest::kNoLeaf);
    REQUIRE(!primary.grid.empty());
    REQUIRE(primary.grid.size() == twin.grid.size());
    std::size_t sampleMismatch = 0;
    std::size_t otherLeaves = 0;
    std::size_t mainMixNotOne = 0;
    for (std::size_t i = 0; i < primary.grid.size(); ++i) {
        if (primary.grid[i].sample != twin.grid[i].sample)
            ++sampleMismatch;
        for (const std::size_t leaf :
             BassMorphTest::differingLeaves(primary.grid[i].afterMotion, twin.grid[i].afterMotion)) {
            if (leaf != mixLeaf)
                ++otherLeaves;
        }
        if (primary.grid[i].afterMotion.excitation.mix != 1.0f)
            ++mainMixNotOne;
    }
    CHECK(sampleMismatch == 0u);
    CHECK(otherLeaves == 0u);
    CHECK(mainMixNotOne == 0u);

    // The handed core excitation: mix 1 in the main render, +0 in the twin, at every interval.
    std::size_t mainRecords = 0;
    std::size_t twinRecords = 0;
    std::size_t mainWrong = 0;
    std::size_t twinWrong = 0;
    for (const ProfundumCallRecord& c : primary.calls) {
        if (c.call != ProfundumCall::CoreExcitation)
            continue;
        ++mainRecords;
        if (c.value != 1.0f)
            ++mainWrong;
    }
    for (const ProfundumCallRecord& c : twin.calls) {
        if (c.call != ProfundumCall::CoreExcitation)
            continue;
        ++twinRecords;
        if (std::bit_cast<std::uint32_t>(c.value) != 0u)
            ++twinWrong;
    }
    INFO("CoreExcitation records: main " << mainRecords << ", twin " << twinRecords);
    CHECK(mainRecords > 0u);
    CHECK(mainRecords == twinRecords);
    CHECK(mainWrong == 0u);
    CHECK(twinWrong == 0u);

    // voiceEngineRenderWithTwin copied the twin's core tap; the excitation changes the main render's core.
    CHECK(veMismatches(primary.linearCoreL, twin.coreL) == 0u);
    CHECK(veMismatches(primary.linearCoreR, twin.coreR) == 0u);
    const std::size_t coreDiff = veMismatches(primary.coreL, twin.coreL);
    INFO("core-tap samples differing from the twin: " << coreDiff);
    CHECK(coreDiff > 0u);
}

TEST_CASE("VoiceEngineHelpers_FrameReaders", "[systems][profundum][voice-engine][fr-086]") {
    // (1) E_sub per frame vs BassMorphTest::eSubDb on a steady 41.20344 Hz sine at 0.5 (eSubDb reads its window
    //     from 1 s, kLowFrequencyFftSize long).
    const std::size_t n = static_cast<std::size_t>(kVeFs) + TestUtils::kLowFrequencyFftSize + 4800;
    const std::vector<float> x = veSine(n, kVeE1Hz, 0.5);
    const double ref = BassMorphTest::eSubDb(x, x, kVeE1Hz, kVeFs);
    const std::vector<VE::VoiceEngineFrame> frames = VE::voiceEngineFrames(x, x, kVeE1Hz, kVeFs);
    REQUIRE(frames.size() > 100u);
    double worst = 0.0;
    for (const VE::VoiceEngineFrame& f : frames)
        worst = std::max(worst, std::abs(f.eSubDb - ref));
    INFO("eSubDb " << ref << " dB (analytic " << 20.0 * std::log10(0.5 / std::sqrt(2.0)) << "), frames "
                   << frames.size() << ", worst |frame - eSubDb| " << worst << " dB");
    CHECK(worst <= 0.01);
    CHECK(static_cast<long long>(frames[0].length) == std::llround(8.0 * kVeFs / kVeE1Hz));
    CHECK(frames[1].start - frames[0].start == VE::kVoiceEngineFrameHop);

    // L_K per frame is kWeightedLevelDb's measurand on that frame.
    for (const std::size_t i : {std::size_t{0}, frames.size() / 2, frames.size() - 1}) {
        const VE::VoiceEngineFrame& f = frames[i];
        const double lk = TestUtils::kWeightedLevelDb(x.data(), f.start + f.length, kVeFs, f.start);
        INFO("frame " << i << ": L_K " << f.lKDb << " vs kWeightedLevelDb " << lk);
        CHECK(std::abs(f.lKDb - lk) <= 1.0e-9);
    }

    // (2) D_nl: a sine with its 10th harmonic at -20 dB re h1, over the "twin" carrying it at -60 dB: 40 dB.
    const auto m = static_cast<std::size_t>(kVeFs);
    const std::vector<float> ch = veSine(m, kVeE1Hz, 0.5, 0.05);
    const std::vector<float> tw = veSine(m, kVeE1Hz, 0.5, 0.0005);
    const std::vector<double> dnl =
        VE::voiceEngineFrameDnlDb(VE::voiceEngineFrames(ch, ch, kVeE1Hz, kVeFs, 0, m, false),
                                  VE::voiceEngineFrames(tw, tw, kVeE1Hz, kVeFs, 0, m, false));
    REQUIRE(!dnl.empty());
    double worstDnl = 0.0;
    for (const double d : dnl)
        worstDnl = std::max(worstDnl, std::abs(d - 40.0));
    INFO("D_nl frames " << dnl.size() << ", first " << dnl[0] << " dB, worst |D_nl - 40| " << worstDnl << " dB");
    CHECK(worstDnl <= 0.1);

    // (3) The boxcar's analytic attenuation at 0.1 Hz with an 8-period E1 frame.
    const double att = VE::voiceEngineBoxcarAttenuation(0.1, kVeE1Hz);
    INFO("boxcar attenuation " << att);
    CHECK(std::abs(att - 0.999380) <= 1.0e-6);
}

TEST_CASE("VoiceEngineHelpers_DropDescriptorsKnownCurve", "[systems][profundum][voice-engine][fr-086]") {
    // d[n] = 24 e^{-n / (0.05 * 48000)}: A = 24 st, tau = 50 ms, 48 kHz, 1 s.
    constexpr double kA = 24.0;
    const double tauSamples = 0.05 * kVeFs;
    std::vector<double> d(static_cast<std::size_t>(kVeFs), 0.0);
    for (std::size_t i = 0; i < d.size(); ++i)
        d[i] = kA * std::exp(-static_cast<double>(i) / tauSamples);
    const double N = 0.2 * kVeFs;   // the alpha span [0, round(N)]

    const VE::VoiceEngineDropDescriptors desc = VE::voiceEngineDropDescriptors(d, kA, N);

    CHECK(desc.start == 24.0);
    const double tsAnalytic = tauSamples * std::log(kA / 0.01);   // 18 679.7 samples
    INFO("t_s " << desc.ts << " samples, analytic " << tsAnalytic);
    CHECK(std::abs(desc.ts - tsAnalytic) <= 2.0);

    const auto last = static_cast<std::size_t>(std::llround(N));
    double sum = 0.0;
    for (std::size_t i = 0; i <= last; ++i)
        sum += d[i];
    const double alphaRef = sum / (static_cast<double>(last + 1) * kA);
    INFO("alpha " << desc.alpha << ", recomputed " << alphaRef);
    CHECK(std::abs(desc.alpha - alphaRef) <= 1.0e-12);

    // A monotone fall never lands at or below 0: no undershoot.
    CHECK(desc.eps == 0.0);
    CHECK(desc.rho == 0.0);
}

TEST_CASE("VoiceEngineHelpers_DeltaBoundOnKnownSignal", "[systems][profundum][voice-engine][fr-086]") {
    const std::size_t ld = VE::voiceEngineDeclickLength(kVeFs);
    REQUIRE(ld == 144u);   // ruling R-B (2026-10-20): the voice's 3 ms dip, not Phase 4's 1.5 ms default
    CHECK(std::abs(VE::voiceEngineDeclickSlope(kVeFs) - kVePi / 288.0) <= 1.0e-15);

    constexpr double kAmp = 0.5;
    const auto total = static_cast<std::size_t>(kVeFs);
    const double w = VE::kTwoPi * kVeE1Hz / kVeFs;
    const double period = kVeFs / kVeE1Hz;
    const auto n0 = static_cast<std::size_t>(std::llround(10.25 * period));   // a peak of the old note
    const std::size_t n1 = n0 + ld;                                             // the Reset restart (phase 0)

    // The steady reference: the same sine, uninterrupted.
    const std::vector<float> steadySig = veSine(total, kVeE1Hz, kAmp, 0.0, 0.0);
    const VE::VoiceEngineSteady steady = VE::voiceEngineSteadyOf(steadySig);
    CHECK(std::abs(steady.pMax - kAmp) <= 1.0e-3);

    // Declicked: the raised-cosine dip g[k] = 0.5 (1 + cos(pi (k + 1) / L_d)) (retrigger_declick.h:11), reaching
    // 0 at its last sample, then the new note from phase 0. Hard: the dip replaced by a step to 0.
    std::vector<float> dip(total, 0.0f);
    std::vector<float> hard(total, 0.0f);
    for (std::size_t n = 0; n < total; ++n) {
        double y = 0.0;
        double h = 0.0;
        if (n < n0) {
            y = kAmp * std::sin(w * static_cast<double>(n));
            h = y;
        } else if (n < n1) {
            const auto k = static_cast<double>(n - n0);
            const double g = 0.5 * (1.0 + std::cos(kVePi * (k + 1.0) / static_cast<double>(ld)));
            y = kAmp * std::sin(w * static_cast<double>(n)) * g;
        } else {
            y = kAmp * std::sin(w * static_cast<double>(n - n1));
            h = y;
        }
        dip[n] = static_cast<float>(y);
        hard[n] = static_cast<float>(h);
    }

    const double bound = VE::voiceEngineDeltaBound(std::span<const VE::VoiceEngineSteady>(&steady, 1),
                                                   VE::voiceEngineDeclickSlope(kVeFs));
    const VE::VoiceEngineWindow all{0, total};
    const double worstDip = VE::voiceEngineMaxDelta(dip, all);
    const double worstHard = VE::voiceEngineMaxDelta(hard, all);
    REQUIRE(worstDip > 0.0);
    const double margin = bound / worstDip;
    INFO("bound " << bound << " (D " << steady.dMax << ", P " << steady.pMax << "), dip max|dy| " << worstDip
                  << " (margin " << margin << "), hard step max|dy| " << worstHard);
    CHECK(margin >= 1.0);
    CHECK(worstHard > bound);

    // Window W (SC-005) around the restart, and the relative reading against the declicked signal.
    const VE::VoiceEngineWindow win = VE::voiceEngineWindow(n0, n1, kVeE1Hz, kVeE1Hz, kVeFs);
    CHECK(win.begin == n0);
    CHECK(win.end == n1 + static_cast<std::size_t>(std::llround(period)));
    CHECK(VE::voiceEngineDeltaRatio(hard, dip, win) > 1.0);
}
