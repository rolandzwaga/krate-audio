// ==============================================================================
// Profundum - main-page audibility rows (Phase 12 T039; SC-006 (a), ruled 2026-10-24)
// ==============================================================================
// Profundum_MainPageAudibility: the five Main IDs D3 found without a gated row (1, 200, 201, 204, 1000), each through
//   the processor path only (ppRenderProcessor, param_path_audibility_helpers.h): the base endpoints are loaded through
//   setState, every value is a normalised parameter change, the descriptor is read on the Sum tap over the held
//   window [1.0 s, 2.0 s), at E1 and C3, 48 kHz, host block 64. Bars are the Phase 9 descriptor families' own:
//   E_sub >= 3 dB (1), D_cx >= 3 dB (200, 201), D_br >= 0.25 oct (204), the AT-10 anchor-level E_sub >= 2 dB (1000).
//   1001 Edit Target is exempt (routing control; its audibility is AT-41 / AT-42).
// Phase 14 T020 (FR-031 as amended by R6; SC-017 (c), SC-023 (c); plan 5.6, P-6): rows 1003 Morph Mod Source and
//   1004 Morph Mod Amount on the AT-41 showcase pair (auStateAs() / auStateB() through setState), Morph Position 0,
//   D_nl (auBm::dNlDb) on the Char / twin tap, the twin carrying the processor engine's modulation (T019). The read
//   window is the D_nl reader's frame [1.0 s, 1.0 s + W), W = kLowFrequencyFftSize (bass_morph_test_helpers.h:703-707),
//   so each arm renders 1.0 s + W + 0.25 s. Preconditions (REQUIRE, before the bar): over that frame the grid-record
//   resolved.position varies <= 0.01 in both arms, is >= 0.9 in the hi arm and <= 0.01 in the lo arm.
// ==============================================================================

#include "param_path_audibility_helpers.h"
#include "profundum_test_fixture.h"

#include "parameters/morph_mod_params.h"
#include "parameters/morph_params.h"
#include "plugin_ids.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <numeric>
#include <vector>

using namespace ProfundumPluginTest;

namespace {

namespace P = ::Profundum;

constexpr double kMpReadFromS = 1.0;
constexpr double kMpReadToS = 2.0;
constexpr double kMpPolySecondOnS = 1.0;
constexpr double kMpPolyReadFromS = 1.25;
constexpr double kMpESubBarDb = 3.0;      // E_sub family (AT-47 / AT-26)
constexpr double kMpDcxBarDb = 3.0;       // D_cx family (AT-14 - AT-17)
constexpr double kMpDbrBarOct = 0.25;     // D_br family (AT-48)
constexpr double kMpAnchorBarDb = 2.0;    // AT-10 anchor-level E_sub

[[nodiscard]] std::size_t mpSample(double seconds) { return static_cast<std::size_t>(auAt(seconds)); }

/// The AT-02 base written through the host: kSawAnchor with Edge 0.5, one control overridden.
[[nodiscard]] std::vector<PpParam> mpSawBase(Steinberg::Vst::ParamID id, double plain) {
    std::vector<PpParam> params = ppBaseParams(PpBase::AsSawEdgeHalf);
    for (PpParam& p : params) {
        if (p.id == id) {
            p.norm = ppNorm(id, plain);
        }
    }
    return params;
}

[[nodiscard]] auPt::VoiceEngineTaps mpRender(std::uint8_t note, const Kd::MorphState& a, const Kd::MorphState& b,
                                             std::vector<PpParam> params) {
    const PpArm arm = ppArm(auOne(auStateAs()), note, auAt(kMpReadToS), 0, a, b, std::move(params));
    return ppRender(PpPath::Processor, arm, false);
}

[[nodiscard]] double mpMeanFrames(const auPt::VoiceEngineTaps& t, double f0, AuFrameDesc d) {
    const std::vector<double> v =
        auFrameSeries(t.sumL, t.sumR, f0, mpSample(kMpReadFromS), mpSample(kMpReadToS), d);
    REQUIRE(!v.empty());
    return std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
}

[[nodiscard]] double mpESub(const auPt::VoiceEngineTaps& t, double f0, double fromS) {
    const std::size_t from = mpSample(fromS);
    return auWindowESubDb(t.sumL, t.sumR, from, mpSample(kMpReadToS) - from, f0);
}

void mpReport(const char* id, std::uint8_t note, double lo, double hi, double value, double bar) {
    std::cout << "Profundum_MainPageAudibility " << id << " " << auNoteName(note) << ": lo " << lo << " hi " << hi
              << " value " << value << " bar " << bar << '\n';
    INFO(id << " " << auNoteName(note) << ": lo " << lo << ", hi " << hi << ", |diff| " << value << ", bar " << bar);
    CHECK(auFinite(value));
    CHECK(value >= bar);
}

/// A recipe lever on the AT-02 base: lo = control at 0, hi = control at 1, the mean frame descriptor's |difference|.
void mpRecipeRow(const char* id, Steinberg::Vst::ParamID param, AuFrameDesc d, double bar) {
    for (const std::uint8_t note : {kAuE1, kAuC3}) {
        const double f0 = auHz(note);
        const double lo = mpMeanFrames(mpRender(note, auStateAs(), auStateAs(), mpSawBase(param, 0.0)), f0, d);
        const double hi = mpMeanFrames(mpRender(note, auStateAs(), auStateAs(), mpSawBase(param, 1.0)), f0, d);
        mpReport(id, note, lo, hi, std::abs(hi - lo), bar);
    }
}

// --- Phase 14 T020: rows 1003 / 1004 (FR-031 as amended by R6) ---------------------------------------------------

constexpr std::uint64_t kMpDnlFrame = Krate::DSP::TestUtils::kLowFrequencyFftSize;   // W = 262 144 samples
constexpr double kMpMorphTailS = 0.25;
constexpr double kMpDnlBarDb = 3.0;                // D_nl family (AT-41)
constexpr double kMpMorphPosStability = 0.01;      // FR-031: max - min of resolved.position over the frame
constexpr double kMpMorphPosHiMin = 0.9;           // FR-031: the hi arm reached B
constexpr double kMpMorphPosLoMax = 0.01;          // FR-031: the lo arm stays at A
constexpr double kMpTwinPosTolerance = 1e-6;       // SC-023 (b) / (c)
static_assert(Krate::DSP::MassDynamicsTest::kSettleSeconds == kMpReadFromS,
              "R6: the rows read the D_nl reader's frame, which starts at kSettleSeconds");

/// 1003 is a list (Off, Envelope, Cycle, Velocity, Mod Wheel; morph_mod_params.h:59-62): index / (count - 1). ppNorm
/// carries no law for 1003 / 1004, so the rows write the normalised values FR-031 pins directly.
[[nodiscard]] double mpSourceNorm(int index) {
    return static_cast<double>(index) / static_cast<double>(P::kNumMorphModSources - 1);
}

/// Render length: 1.0 s + W + 0.25 s, on the 32-sample grid.
[[nodiscard]] std::uint64_t mpMorphModTotal() {
    const std::uint64_t n = auAt(kMpReadFromS) + kMpDnlFrame + auAt(kMpMorphTailS);
    return ((n + kAuGrid - 1) / kAuGrid) * kAuGrid;
}

/// One arm on the AT-41 pair: A_s / B loaded through setState, the levers as normalised changes; @p velocity127 sets
/// every script note-on to velocity 127 (the 1004 row), which the processor path sends as 127 / 127.
[[nodiscard]] PpArm mpMorphModArm(std::uint8_t note, std::vector<PpParam> params, bool velocity127) {
    const AuSetting direct{.a = auStateAs(), .b = auStateB(), .position = 0.0f};
    PpArm arm = ppArm(direct, note, mpMorphModTotal(), 0, auStateAs(), auStateB(), std::move(params));
    if (velocity127) {
        for (auPt::VoiceEngineEvent& ev : arm.direct.events) {
            if (ev.kind == AuKind::NoteOn) {
                ev.velocity = 127;
            }
        }
    }
    return arm;
}

/// The grid-record resolved.position range and the mean smoothedMass over the D_nl reader's frame [1.0 s, 1.0 s + W).
struct MpFrameSpan {
    std::size_t n = 0;
    double posMin = 0.0;
    double posMax = 0.0;
    double posMean = 0.0;
    double massMean = 0.0;
};

[[nodiscard]] MpFrameSpan mpFrameSpan(const auPt::VoiceEngineTaps& t) {
    const std::uint64_t from = auAt(kMpReadFromS);
    const std::uint64_t to = from + kMpDnlFrame;
    MpFrameSpan s;
    double posSum = 0.0;
    double massSum = 0.0;
    for (const auPt::VoiceEngineGridRecord& r : t.grid) {
        if (r.sample < from || r.sample >= to) {
            continue;
        }
        const auto p = static_cast<double>(r.resolved.position);
        s.posMin = s.n == 0u ? p : std::min(s.posMin, p);
        s.posMax = s.n == 0u ? p : std::max(s.posMax, p);
        posSum += p;
        massSum += static_cast<double>(r.smoothedMass);
        ++s.n;
    }
    if (s.n > 0u) {
        s.posMean = posSum / static_cast<double>(s.n);
        s.massMean = massSum / static_cast<double>(s.n);
    }
    return s;
}

/// FR-031 preconditions (non-vacuity), REQUIREd before the bar.
void mpRequireMorphFrame(const char* id, std::uint8_t note, const MpFrameSpan& lo, const MpFrameSpan& hi) {
    INFO(id << " " << auNoteName(note) << ": lo position [" << lo.posMin << ", " << lo.posMax << "] (" << lo.n
            << " grid points), hi position [" << hi.posMin << ", " << hi.posMax << "] (" << hi.n << " grid points)");
    REQUIRE(lo.n > 0u);
    REQUIRE(hi.n > 0u);
    REQUIRE(lo.posMax - lo.posMin <= kMpMorphPosStability);
    REQUIRE(hi.posMax - hi.posMin <= kMpMorphPosStability);
    REQUIRE(hi.posMin >= kMpMorphPosHiMin);
    REQUIRE(lo.posMax <= kMpMorphPosLoMax);
}

/// SC-023 (c) needs the twin's grid records, which ppRenderProcessor drops after copying the twin's core tap
/// (param_path_audibility_helpers.h:639-641; that header is outside T020's file list). This repeats its steps 1-6
/// (:577-648) unchanged, the twin built from the same engine read-backs (morphBasePosition, morphModulation,
/// morphModWheel, morphModBypass), and returns the processor's taps and the whole twin render.
struct MpTwinRender {
    auPt::VoiceEngineTaps processor;
    auPt::VoiceEngineTaps twin;
};

[[nodiscard]] MpTwinRender mpRenderProcessorWithTwinGrid(const PpArm& arm) {
    const auPt::VoiceEngineScript& s = arm.direct;
    auto fx = std::make_unique<ProcessorFixture>();

    const StateBytes state = withEndpoints(writeV2Stream(*fx->proc), arm.hostA, arm.hostB);
    REQUIRE(loadStateBytes(*fx->proc, state) == Steinberg::kResultOk);

    MultiParamChanges pc;
    pc.reserve(arm.params.size() + 1u);
    for (const double target : {0.0, 1.0}) {
        pc.clear();
        pc.addQueue(P::kMorphEditTargetId).addTestPoint(0, target);
        for (const PpParam& p : arm.params) {
            pc.addQueue(p.id).addTestPoint(0, p.norm);
        }
        REQUIRE(fx->processNoOutputs(&pc) == Steinberg::kResultOk);
    }

    MpTwinRender out;
    auPt::voiceEngineAllocateTaps(out.processor, s);
    auPt::VoiceEngineObserver observer(out.processor);
    fx->proc->setEngineObserverForTest(&observer);
    REQUIRE(fx->prepare(s.fs, static_cast<Steinberg::int32>(s.hostBlock)) == Steinberg::kResultOk);

    std::vector<ProcessorFixture::ScriptedEvent> events;
    for (const auPt::VoiceEngineEvent& ev : s.events) {
        if (ev.kind != AuKind::NoteOn && ev.kind != AuKind::NoteOff) {
            continue;
        }
        ProcessorFixture::ScriptedEvent e{};
        e.sample = static_cast<Steinberg::int64>(ev.sample);
        e.kind = ev.kind == AuKind::NoteOn ? ProcessorFixture::ScriptedEvent::Kind::NoteOn
                                           : ProcessorFixture::ScriptedEvent::Kind::NoteOff;
        e.pitch = static_cast<Steinberg::int16>(ev.note);
        e.velocity = static_cast<float>(ev.velocity) / 127.0f;
        events.push_back(e);
    }
    const auto total = static_cast<std::size_t>(s.totalSamples);
    fx->renderScript(events, total, s.hostBlock);
    REQUIRE(fx->capturedL.size() == total);
    REQUIRE(fx->proc->endpointRejectionsForTest() == 0u);

    REQUIRE(fx->engine() != nullptr);
    auPt::VoiceEngineScript t = s;
    t.a = fx->engine()->getEndpoint(Kd::BassMorphEngine::Endpoint::A);
    t.b = fx->engine()->getEndpoint(Kd::BassMorphEngine::Endpoint::B);
    t.position = fx->engine()->morphBasePosition();
    t.morphMod = fx->engine()->morphModulation();
    t.morphModWheel = fx->engine()->morphModWheel();
    t.morphModBypass = fx->engine()->morphModBypass();
    t.config = ::Profundum::makeProfundumEngineConfig();
    out.twin = auPt::voiceEngineRender(auPt::voiceEngineTwinScript(t));

    fx->proc->setActive(0u);
    fx->proc->setEngineObserverForTest(nullptr);
    return out;
}

/// SC-023 (c): at every processor grid point of [1.0 s, 1.0 s + W), the twin's resolved.position at the same sample
/// equals the processor's within 1e-6.
void mpRequireTwinFollowsProcessor(const MpTwinRender& r, std::uint8_t note) {
    const std::uint64_t from = auAt(kMpReadFromS);
    const std::uint64_t to = from + kMpDnlFrame;
    const auto bySample = [](const auPt::VoiceEngineGridRecord& g, std::uint64_t s) { return g.sample < s; };
    std::size_t compared = 0;
    std::size_t missing = 0;
    double worst = 0.0;
    for (const auPt::VoiceEngineGridRecord& g : r.processor.grid) {
        if (g.sample < from || g.sample >= to) {
            continue;
        }
        const auto it = std::lower_bound(r.twin.grid.begin(), r.twin.grid.end(), g.sample, bySample);
        if (it == r.twin.grid.end() || it->sample != g.sample) {
            ++missing;
            continue;
        }
        worst = std::max(worst, std::abs(static_cast<double>(it->resolved.position)
                                         - static_cast<double>(g.resolved.position)));
        ++compared;
    }
    std::cout << "Profundum_MainPageAudibility 1003 SC-023 (c) " << auNoteName(note) << ": " << compared
              << " grid points, " << missing << " without a twin record, worst |p_twin - p_proc| " << worst << '\n';
    INFO("1003 SC-023 (c) " << auNoteName(note) << ": compared " << compared << ", missing " << missing << ", worst "
                            << worst);
    REQUIRE(compared > 0u);
    REQUIRE(missing == 0u);
    REQUIRE(worst <= kMpTwinPosTolerance);
}

}  // namespace

TEST_CASE("Profundum_MainPageAudibility", "[profundum][audibility]") {
    SECTION("1 Voice Mode: Mono -> Poly, first note held, second added at 1.0 s (E_sub at the first f0, Sum)") {
        for (const std::uint8_t note : {kAuE1, kAuC3}) {
            const std::uint8_t second = note == kAuE1 ? kAuC3 : kAuC4;
            const double f0 = auHz(note);
            double reading[2] = {0.0, 0.0};
            for (const int poly : {0, 1}) {
                PpArm arm = ppArm(auOne(auStateAs()), note, auAt(kMpReadToS), 0, auStateAs(), auStateAs(),
                                  {{.id = P::kVoiceModeId, .norm = ppNorm(P::kVoiceModeId, poly)}});
                arm.direct.events.push_back(auOn(auAt(kMpPolySecondOnS), second));
                reading[poly] = mpESub(ppRender(PpPath::Processor, arm, false), f0, kMpPolyReadFromS);
            }
            mpReport("1", note, reading[0], reading[1], std::abs(reading[1] - reading[0]), kMpESubBarDb);
        }
    }
    SECTION("200 Depth: 0 -> 1 on kSawAnchor, Edge 0.5 (mean frame D_cx, Sum)") {
        mpRecipeRow("200", P::kCoreDepthId, AuFrameDesc::DCx, kMpDcxBarDb);
    }
    SECTION("201 Body: 0 -> 1 on kSawAnchor, Edge 0.5 (mean frame D_cx, Sum)") {
        mpRecipeRow("201", P::kCoreBodyId, AuFrameDesc::DCx, kMpDcxBarDb);
    }
    SECTION("204 Edge: 0 -> 1 on kSawAnchor (mean frame D_br, Sum)") {
        mpRecipeRow("204", P::kCoreEdgeId, AuFrameDesc::DBr, kMpDbrBarOct);
    }
    SECTION("1000 Anchor Morph Mode: Hold -> Blend at Morph Position 1, A Anchor Level 1, B Anchor Level 0 (E_sub, Sum)") {
        Kd::MorphState a = auStateAs();
        Kd::MorphState b = auStateAs();
        a.anchor.level = 1.0f;
        b.anchor.level = 0.0f;
        REQUIRE(Kd::isValidMorphState(a));
        REQUIRE(Kd::isValidMorphState(b));
        const double maxMode = static_cast<double>(P::kNumAnchorMorphModes - 1);
        for (const std::uint8_t note : {kAuE1, kAuC3}) {
            const double f0 = auHz(note);
            double reading[2] = {0.0, 0.0};
            for (const int mode : {0, 1}) {   // 0 Lock (Hold), 1 Blend
                reading[mode] = mpESub(mpRender(note, a, b,
                                                {{.id = P::kMorphPositionId, .norm = ppNorm(P::kMorphPositionId, 1.0)},
                                                 {.id = P::kMorphAnchorModeId, .norm = mode / maxMode}}),
                                       f0, kMpReadFromS);
            }
            mpReport("1000", note, reading[0], reading[1], std::abs(reading[1] - reading[0]), kMpAnchorBarDb);
        }
    }
    SECTION("1003 Morph Mod Source: Off -> Velocity, Amount +1, every note at velocity 127, Morph Position 0, AT-41 pair "
            "(D_nl, Char / twin)") {
        // R7: Off -> Mod Wheel with the wheel at 1 pushed Mass in the Off arm (C-7) and read 1.28 dB; Velocity has no
        // Mass hand-over, so the Source switch moves Morph alone.
        const auto params = [](int source) {
            return std::vector<PpParam>{{.id = P::kMorphModSourceId, .norm = mpSourceNorm(source)},
                                        {.id = P::kMorphModAmountId, .norm = 1.0},   // Amount +1
                                        {.id = P::kMorphPositionId, .norm = ppNorm(P::kMorphPositionId, 0.0)}};
        };
        const int off = static_cast<int>(P::MorphMod::Source::Off);
        const int velocity = static_cast<int>(P::MorphMod::Source::Velocity);
        for (const std::uint8_t note : {kAuE1, kAuC3}) {
            const double f0 = auHz(note);
            const PpArm hiArm = mpMorphModArm(note, params(velocity), true);
            const auPt::VoiceEngineTaps lo = ppRenderProcessor(mpMorphModArm(note, params(off), true), true);
            const auPt::VoiceEngineTaps hi = ppRenderProcessor(hiArm, true);
            const MpFrameSpan sLo = mpFrameSpan(lo);
            const MpFrameSpan sHi = mpFrameSpan(hi);
            std::cout << "Profundum_MainPageAudibility 1003 " << auNoteName(note) << ": mean smoothedMass lo "
                      << sLo.massMean << " hi " << sHi.massMean << "; mean position lo " << sLo.posMean << " hi "
                      << sHi.posMean << '\n';
            INFO("1003 " << auNoteName(note) << ": the Source switch must not move Mass");
            REQUIRE(std::abs(sLo.massMean - sHi.massMean) <= 1e-6);
            mpRequireMorphFrame("1003", note, sLo, sHi);
            mpRequireTwinFollowsProcessor(mpRenderProcessorWithTwinGrid(hiArm), note);   // SC-023 (c)
            const double dLo = auBm::dNlDb(lo, f0, kAuFs);
            const double dHi = auBm::dNlDb(hi, f0, kAuFs);
            mpReport("1003", note, dLo, dHi, std::abs(dHi - dLo), kMpDnlBarDb);
        }
    }
    SECTION("1004 Morph Mod Amount: 0 -> +1, Source Velocity, every note at velocity 127, Morph Position 0, AT-41 pair "
            "(D_nl, Char / twin)") {
        const auto params = [](double amountNorm) {
            return std::vector<PpParam>{
                {.id = P::kMorphModSourceId, .norm = mpSourceNorm(static_cast<int>(P::MorphMod::Source::Velocity))},
                {.id = P::kMorphModAmountId, .norm = amountNorm},   // 0.5 = Amount 0, 1.0 = Amount +1
                {.id = P::kMorphPositionId, .norm = ppNorm(P::kMorphPositionId, 0.0)}};
        };
        for (const std::uint8_t note : {kAuE1, kAuC3}) {
            const double f0 = auHz(note);
            const auPt::VoiceEngineTaps lo = ppRenderProcessor(mpMorphModArm(note, params(0.5), true), true);
            const auPt::VoiceEngineTaps hi = ppRenderProcessor(mpMorphModArm(note, params(1.0), true), true);
            const MpFrameSpan sLo = mpFrameSpan(lo);
            const MpFrameSpan sHi = mpFrameSpan(hi);
            std::cout << "Profundum_MainPageAudibility 1004 " << auNoteName(note) << ": mean position lo "
                      << sLo.posMean << " hi " << sHi.posMean << '\n';
            mpRequireMorphFrame("1004", note, sLo, sHi);
            const double dLo = auBm::dNlDb(lo, f0, kAuFs);
            const double dHi = auBm::dNlDb(hi, f0, kAuFs);
            mpReport("1004", note, dLo, dHi, std::abs(dHi - dLo), kMpDnlBarDb);
        }
    }
}
