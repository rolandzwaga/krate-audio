// ==============================================================================
// Layer 3: System Tests - Profundum Phase 4 (specs/profundum-phase4-drop-note-behaviour)
// ==============================================================================
// Reference: specs/profundum-phase4-drop-note-behaviour/plan.md (S8, S11.1, S11.2)
//            specs/profundum-phase4-drop-note-behaviour/tasks.md (T006 wires this TU; T019, T020 fill it)
//
// Holds: ProfundumNoteBehaviour_HarnessClassification, dropCheckC1/anchorCheckC1 cross-check, the f_zc
// estimator self-test (D-15), SC-023 enum mirror, SC-012(a) Reset reference (T019); SC-009(b)(c),
// SC-011, SC-012(b)-(d) (+ the [long] GlideLock60s), SC-018, SC-026, the plan S9 mutable-static scan
// (T020). Includes all five Phase 4 headers with the core
// and the Anchor to prove they coexist.
// ==============================================================================

#include "profundum_note_behaviour_test_helpers.h"

#include <krate/dsp/systems/profundum_core.h>
#include <krate/dsp/systems/anchor_oscillator.h>
#include <krate/dsp/core/drop_curve.h>
#include <krate/dsp/primitives/retrigger_declick.h>
#include <krate/dsp/processors/drop_envelope.h>
#include <krate/dsp/processors/drop_phase_lock.h>
#include <krate/dsp/processors/note_memory.h>
#include <krate/dsp/systems/voice_allocator.h>

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
#include <iterator>
#include <memory>
#include <random>
#include <span>
#include <string>
#include <utility>
#include <vector>

using Krate::DSP::AnchorOscillator;
using Krate::DSP::DropCurve;
using Krate::DSP::DropEnvelope;
using Krate::DSP::DropPhaseLock;
using Krate::DSP::GlideMode;
using Krate::DSP::MonoHandler;
using Krate::DSP::NoteMemory;
using Krate::DSP::ProfundumCore;
using Krate::DSP::RetriggerDeclick;
using Krate::DSP::SpectralShapeRecipe;
using Krate::DSP::VoiceAllocator;
using Krate::DSP::VoiceEvent;
namespace PT = Krate::DSP::ProfundumTest;
namespace DT = Krate::DSP::DropTest;

namespace {

constexpr double kNbFs = 48000.0;
constexpr int kNbE1 = 28;
constexpr int kNbC2 = 36;
constexpr int kNbC3 = 48;
constexpr double kNbRadToDeg = 57.295779513082320876798154814105;

std::unique_ptr<PT::NoteBehaviourHarness> makeNbHarness(double fs) {
    auto h = std::make_unique<PT::NoteBehaviourHarness>();
    h->prepare(fs);
    return h;
}

/// Renders one script on a fresh harness and requires the recorded outcomes to equal its list.
void requireScriptClassification(const PT::NoteScript& sc, NoteMemory::Mode m, NoteMemory::Retrigger r,
                                 std::size_t hostBlock, const char* label) {
    auto h = makeNbHarness(kNbFs);
    h->setNoteMemory(m, r);
    PT::NoteBehaviourTraces tr;
    PT::noteBehaviourRunScript(*h, sc, hostBlock, tr);
    std::string recorded;
    for (const PT::NoteOutcomeRecord& rec : tr.recorded()) {
        recorded += PT::noteOutcomeName(rec.outcome);
        recorded += ' ';
    }
    INFO(label << " mode " << static_cast<int>(m) << " retrigger " << static_cast<int>(r) << " recorded: "
               << recorded);
    CHECK(h->contractViolations() == 0);
    REQUIRE(PT::noteOutcomesMatch(sc.expected, tr.recorded()));
}

}  // namespace

// =============================================================================
// Plan S8 harness self-test
// =============================================================================

TEST_CASE("ProfundumNoteBehaviour_HarnessClassification", "[systems][profundum][note-behaviour]") {
    constexpr std::array<NoteMemory::Mode, 4> kModes{NoteMemory::Mode::AlwaysRetrigger, NoteMemory::Mode::Legato,
                                                     NoteMemory::Mode::EnvelopeRetriggerOnly,
                                                     NoteMemory::Mode::OscillatorContinuous};
    constexpr std::array<NoteMemory::Retrigger, 2> kRetriggers{NoteMemory::Retrigger::Reset,
                                                               NoteMemory::Retrigger::Continue};

    SECTION("standard script (SC-009(b), SC-010, SC-012(a), SC-014): every mode x Retrigger") {
        // Includes the positive control: the last noteOff yields Release, then FromSilence after >= 1 s.
        const PT::NoteScript sc = PT::noteBehaviourStandardScript(kNbE1, kNbFs);
        REQUIRE(sc.expected.size() == sc.events.size());
        REQUIRE(sc.expected[5] == PT::NoteOutcome::Release);
        REQUIRE(sc.expected[6] == PT::NoteOutcome::FromSilence);
        REQUIRE(sc.events[6].at - sc.events[5].at
                >= PT::noteBehaviourSeconds(1.0 + PT::NoteBehaviourHarness::kReleaseMs / 1000.0, kNbFs));
        for (const auto m : kModes)
            for (const auto r : kRetriggers)
                requireScriptClassification(sc, m, r, 37, "standard");
    }

    SECTION("matrix script (SC-009(b)): every mode x Retrigger") {
        const PT::NoteScript sc = PT::noteBehaviourMatrixScript(kNbE1, kNbFs);
        REQUIRE(sc.expected.size() == sc.events.size());
        REQUIRE(sc.expected.back() == PT::NoteOutcome::Tail);
        REQUIRE(sc.events.back().note == sc.events[sc.events.size() - 2].note);   // the same-note Tail
        for (const auto m : kModes)
            for (const auto r : kRetriggers)
                requireScriptClassification(sc, m, r, 37, "matrix");
    }

    SECTION("non-active held key release yields NoAction (positive control)") {
        const PT::NoteScript sc = PT::noteBehaviourNonActiveReleaseScript(kNbE1, kNbFs);
        auto h = makeNbHarness(kNbFs);
        PT::NoteBehaviourTraces tr;
        PT::noteBehaviourRunScript(*h, sc, 37, tr);
        REQUIRE(PT::noteOutcomesMatch(sc.expected, tr.recorded()));
        // The harness asserts ev.frequency == the active note's frequency and ev.retrigger == false
        // (counted as contract violations, never inline: the render path is Catch-free).
        CHECK(h->contractViolations() == 0);
        const PT::NoteOutcomeRecord& na = tr.recorded()[2];
        CHECK(na.note == kNbE1 + 4);   // the active note is unchanged
        CHECK(na.effectiveSample == PT::kNoteBehaviourNoSample);   // nothing applied
        for (const auto m : kModes)
            for (const auto r : kRetriggers)
                requireScriptClassification(sc, m, r, 37, "non-active release");
    }

    SECTION("a new note during the dip re-targets the pending transition (SC-011)") {
        const PT::NoteScript sc = PT::noteBehaviourDipRetargetScript(kNbE1, kNbFs);
        for (const auto m : kModes)
            for (const auto r : kRetriggers)
                requireScriptClassification(sc, m, r, 37, "dip retarget");

        auto h = makeNbHarness(kNbFs);   // AlwaysRetrigger + Reset: the Overlap needs a declick
        PT::NoteBehaviourTraces tr;
        PT::noteBehaviourRunScript(*h, sc, 37, tr);
        const auto rec = tr.recorded();
        REQUIRE(rec.size() == 3);
        CHECK(rec[1].actions.declick);
        CHECK(rec[1].superseded);
        CHECK_FALSE(rec[2].superseded);
        CHECK(rec[1].effectiveSample == PT::kNoteBehaviourNoSample);
        // The re-target keeps the switch sample of the first arm.
        const auto ld = static_cast<std::size_t>(
            std::ceil(static_cast<double>(Krate::DSP::RetriggerDeclick::kDeclickMs) * kNbFs / 1000.0));
        CHECK(rec[2].effectiveSample == sc.events[1].at + ld - 1);
        CHECK(rec[2].note == kNbE1 + 7);
    }

    SECTION("POLY VoiceReuse from a 2-voice Hard-steal VoiceAllocator (SC-009(c))") {
        VoiceAllocator alloc;
        (void)alloc.setVoiceCount(2);
        alloc.setStealMode(Krate::DSP::StealMode::Hard);
        const auto e1 = alloc.noteOn(40, 100);
        std::vector<VoiceEvent> ev1(e1.begin(), e1.end());
        const auto e2 = alloc.noteOn(45, 100);
        std::vector<VoiceEvent> ev2(e2.begin(), e2.end());
        const auto e3 = alloc.noteOn(50, 100);
        std::vector<VoiceEvent> ev3(e3.begin(), e3.end());
        const auto steal = std::find_if(ev3.begin(), ev3.end(),
                                        [](const VoiceEvent& e) { return e.type == VoiceEvent::Type::Steal; });
        REQUIRE(steal != ev3.end());
        const std::size_t voice = steal->voiceIndex;

        auto h = makeNbHarness(kNbFs);
        PT::NoteBehaviourTraces tr;
        const std::size_t total = PT::noteBehaviourSeconds(1.0, kNbFs);
        tr.allocate(total);
        h->queueVoiceEvents(ev1, voice, 0);
        h->queueVoiceEvents(ev2, voice, 0);
        h->queueVoiceEvents(ev3, voice, PT::noteBehaviourSeconds(0.5, kNbFs));
        h->render(total, 37, tr);
        const std::array<PT::NoteOutcome, 2> expected{PT::NoteOutcome::FromSilence, PT::NoteOutcome::VoiceReuse};
        CHECK(h->contractViolations() == 0);
        REQUIRE(PT::noteOutcomesMatch(expected, tr.recorded()));
        CHECK(tr.recorded()[1].note == 50);
    }

    SECTION("an injected misclassification fails the comparison (positive control)") {
        const PT::NoteScript sc = PT::noteBehaviourStandardScript(kNbE1, kNbFs);
        auto h = makeNbHarness(kNbFs);
        PT::NoteBehaviourTraces tr;
        PT::noteBehaviourRunScript(*h, sc, 37, tr);
        REQUIRE(PT::noteOutcomesMatch(sc.expected, tr.recorded()));
        for (std::size_t i = 0; i < sc.expected.size(); ++i) {
            std::vector<PT::NoteOutcome> altered = sc.expected;
            altered[i] = altered[i] == PT::NoteOutcome::Overlap ? PT::NoteOutcome::Tail : PT::NoteOutcome::Overlap;
            INFO("altered element " << i);
            CHECK_FALSE(PT::noteOutcomesMatch(altered, tr.recorded()));
        }
        std::vector<PT::NoteOutcome> shorter(sc.expected.begin(), sc.expected.end() - 1);
        CHECK_FALSE(PT::noteOutcomesMatch(shorter, tr.recorded()));
    }
}

// =============================================================================
// Plan S11.2: dropCheckC1 <-> anchorCheckC1 cross-check
// =============================================================================

TEST_CASE("ProfundumNoteBehaviour_C1CheckerCrossCheck", "[systems][profundum][note-behaviour]") {
    using Spec = PT::AnchorSegmentSpec;
    using Kind = Spec::Kind;

    const auto buildSegments = [](double hermiteSlope) {
        return std::vector<Spec>{
            Spec{Kind::Hold, 0.2, 0.0, 0.0, 0.2, 0, 50},
            Spec{Kind::Cosine, 0.2, 0.6, 0.0, 0.8, 50, 100},
            Spec{Kind::Hold, 0.8, 0.0, 0.0, 0.8, 150, 40},
            Spec{Kind::Hermite, 0.8, 0.0, hermiteSlope, 0.3, 190, 120},
            Spec{Kind::Hold, 0.3, 0.0, 0.0, 0.3, 310, 40},
        };
    };
    const auto synth = [](const std::vector<Spec>& segs) {
        const Spec& last = segs.back();
        std::vector<double> g(last.start + last.len + 2, last.g0);
        for (const Spec& s : segs) {
            for (std::size_t k = 0; k <= s.len; ++k) {
                double v = s.g0;
                if (s.kind == Kind::Cosine)
                    v = PT::anchorRaisedCosine(s.g0, s.a, k, s.len);
                else if (s.kind == Kind::Hermite)
                    v = PT::anchorHermite(s.g0, s.gp0, s.g1, k, s.len);
                g[s.start + k] = v;
            }
        }
        return g;
    };
    const auto compare = [](const std::vector<double>& g, const std::vector<Spec>& segs, double lo, double hi,
                            const char* label) {
        std::vector<DT::DropC1Segment> dsegs;
        for (const Spec& s : segs)
            dsegs.push_back(DT::DropC1Segment{s.start, s.len, PT::anchorSegmentD2Bound(s)});
        const PT::C1Report a = PT::anchorCheckC1(g, segs, lo, hi);
        const DT::DropC1Report d = DT::dropCheckC1(g, dsegs, lo, hi);
        INFO(label << ": anchor pass " << a.pass << " ratio " << a.worstRatio << " jump " << a.worstBoundaryJump
                   << " | drop pass " << d.pass << " ratio " << d.worstRatio << " jump " << d.worstBoundaryJump);
        CHECK(a.pass == d.pass);
        CHECK(a.inRange == d.inRange);
        CHECK(std::abs(a.worstRatio - d.worstRatio) <= 1e-12);
        CHECK(std::abs(a.worstBoundaryJump - d.worstBoundaryJump) <= 1e-12);
        return a.pass;
    };

    // Clean C1 signal: both pass.
    {
        const auto segs = buildSegments(0.0);
        const auto g = synth(segs);
        CHECK(compare(g, segs, 0.0, 1.0, "clean"));
    }
    // A bump inside the raised cosine: both fail with the same ratio.
    {
        const auto segs = buildSegments(0.0);
        auto g = synth(segs);
        g[100] += 1e-3;
        CHECK_FALSE(compare(g, segs, 0.0, 1.0, "interior bump"));
    }
    // A slope corner at the Hold -> Hermite boundary: both fail on the boundary jump.
    {
        const auto segs = buildSegments(-0.01);
        const auto g = synth(segs);
        CHECK_FALSE(compare(g, segs, -1.0, 2.0, "boundary corner"));
    }
    // Out of range: both fail on the range.
    {
        const auto segs = buildSegments(0.0);
        const auto g = synth(segs);
        CHECK_FALSE(compare(g, segs, 0.25, 1.0, "range"));
    }
    // Positive control (plan S11.2): a cornered linear ramp (0 -> 1 over 100 samples, then held) fails.
    {
        std::vector<double> g(160, 1.0);
        for (std::size_t n = 0; n <= 100; ++n)
            g[n] = static_cast<double>(n) / 100.0;
        const std::vector<Spec> segs{Spec{Kind::Hermite, 0.0, 0.0, 0.01, 1.0, 0, 100},
                                     Spec{Kind::Hold, 1.0, 0.0, 0.0, 1.0, 100, 50}};
        CHECK_FALSE(compare(g, segs, 0.0, 1.0, "cornered ramp"));
    }

    // dropHermitePeakD2 == anchorHermitePeakD2 on 1 000 random (g0, gp0, g1, len).
    std::mt19937 rng(0x5EED0019u);
    std::uniform_real_distribution<double> val(-48.0, 48.0);
    std::uniform_real_distribution<double> slope(-1.0, 1.0);
    std::uniform_int_distribution<std::size_t> len(1, 100000);
    std::size_t mismatches = 0;
    for (int i = 0; i < 1000; ++i) {
        const double g0 = val(rng);
        const double gp0 = slope(rng);
        const double g1 = val(rng);
        const std::size_t L = len(rng);
        if (!(DT::dropHermitePeakD2(g0, gp0, g1, L) == PT::anchorHermitePeakD2(g0, gp0, g1, L)))
            ++mismatches;
    }
    CHECK(mismatches == 0);
}

// =============================================================================
// D-15: f_zc estimator self-test
// =============================================================================

TEST_CASE("ProfundumNoteBehaviour_PitchEstimatorSelfTest", "[systems][profundum][note-behaviour]") {
    constexpr std::array<double, 4> kShapes{0.0, 1.0 / 3.0, 2.0 / 3.0, 1.0};   // the four Shape anchors
    constexpr std::array<double, 2> kAmounts{12.0, 48.0};
    constexpr std::array<double, 2> kTimesMs{10.0, 1000.0};
    constexpr std::array<int, 2> kNotes{kNbE1, kNbC2};
    constexpr double kBarCents = 1.0;

    double worstCents = 0.0;
    for (const double shape : kShapes) {
        for (const double A : kAmounts) {
            for (const double T : kTimesMs) {
                for (const int note : kNotes) {
                    const DropCurve curve = Krate::DSP::makeDropCurve(A, T, shape, 0.0, kNbFs, false, 0.0, 0.0);
                    REQUIRE(curve.endSample > 0.0);
                    const double fNote = static_cast<double>(PT::anchorMidiHz(note));
                    const std::size_t n = static_cast<std::size_t>(std::ceil(curve.endSample))
                                          + PT::noteBehaviourSeconds(0.3, kNbFs);

                    // Chirp in double: phi[k + 1] = phi[k] + f_ref[k] / fs, x[k] = sin(2 pi phi[k]), stored as float.
                    std::vector<double> fRef(n, 0.0);
                    std::vector<double> phi(n + 1, 0.0);
                    std::vector<float> x(n, 0.0f);
                    for (std::size_t k = 0; k < n; ++k) {
                        fRef[k] = fNote * std::exp2(curve.valueAt(static_cast<double>(k)) / 12.0);
                        x[k] = static_cast<float>(std::sin(PT::kTwoPi * phi[k]));
                        phi[k + 1] = phi[k] + fRef[k] / kNbFs;
                    }
                    // The chirp's phase is piecewise linear in t (f_ref[k] held over [k, k + 1]).
                    const auto phaseAt = [&](double t) {
                        const auto k = static_cast<std::size_t>(std::floor(t));
                        return phi[k] + (t - static_cast<double>(k)) * fRef[k] / kNbFs;
                    };

                    const auto est = PT::noteBehaviourZeroCrossingPitch(x, kNbFs);
                    REQUIRE(est.size() > 10);
                    double cellWorst = 0.0;
                    for (const auto& e : est) {
                        const double mean = (phaseAt(e.tB) - phaseAt(e.tA)) * kNbFs / (e.tB - e.tA);
                        const double cents = 1200.0 * std::log2(e.hz / mean);
                        cellWorst = std::max(cellWorst, std::abs(cents));
                    }
                    INFO("shape " << shape << " A " << A << " T " << T << " ms note " << note << ": worst "
                                  << cellWorst << " cents over " << est.size() << " half-periods");
                    CHECK(cellWorst <= kBarCents);
                    worstCents = std::max(worstCents, cellWorst);
                }
            }
        }
    }
    std::printf("D-15 f_zc self-test: worst |f_zc - half-period mean| %.6f cents (bar %.1f)\n", worstCents,
                kBarCents);
}

// =============================================================================
// SC-023: enum and constant mirror (include discipline; plan S1 C-14)
// =============================================================================

TEST_CASE("NoteMemory_PhasePolicyMirror", "[systems][profundum][note-behaviour]") {
    using PP = NoteMemory::PhasePolicy;
    using CRP = ProfundumCore::RetriggerPhase;
    using ARP = AnchorOscillator::RetriggerPhase;
    static_assert(static_cast<std::uint8_t>(PP::Reset) == static_cast<std::uint8_t>(CRP::Reset));
    static_assert(static_cast<std::uint8_t>(PP::Reset) == static_cast<std::uint8_t>(ARP::Reset));
    static_assert(static_cast<std::uint8_t>(PP::FreeRunning) == static_cast<std::uint8_t>(CRP::FreeRunning));
    static_assert(static_cast<std::uint8_t>(PP::FreeRunning) == static_cast<std::uint8_t>(ARP::FreeRunning));
    static_assert(DropPhaseLock::kPitchUpdateInterval == ProfundumCore::kPitchUpdateInterval
                  && DropPhaseLock::kPitchUpdateInterval == AnchorOscillator::kPitchUpdateInterval);
    // C-14: the value NoteBehaviourHarness passes as minF0Hz.
    static_assert(ProfundumCore::kMinF0Hz == 8.0f);
    // RULING 2026-10-12 (O-1): no sigma_up aliases exist, so none is asserted.

    // The harness hands DropPhaseLock the core's own cap (C-14).
    auto h = makeNbHarness(kNbFs);
    CHECK(h->capHz() == Krate::DSP::SpectralShapeRecipe::capFrequency(kNbFs));
}

// =============================================================================
// SC-012(a): Reset reference
// =============================================================================

TEST_CASE("ProfundumNoteBehaviour_ResetReference", "[systems][profundum][note-behaviour]") {
    using Call = PT::NoteBehaviourCall;
    constexpr double kBarDeg = 1.0;

    for (const int n1 : {kNbE1, kNbC3}) {
        auto h = makeNbHarness(kNbFs);   // AlwaysRetrigger + Reset (NoteMemory defaults), A = 0
        h->setNoteMemory(NoteMemory::Mode::AlwaysRetrigger, NoteMemory::Retrigger::Reset);
        h->setDrop(0.0f, 100.0f, 0.0f, 0.0f);
        const PT::NoteScript sc = PT::noteBehaviourStandardScript(n1, kNbFs);
        PT::NoteBehaviourTraces tr;
        PT::noteBehaviourRunScript(*h, sc, 37, tr);
        REQUIRE(h->contractViolations() == 0);
        REQUIRE(PT::noteOutcomesMatch(sc.expected, tr.recorded()));
        PT::requireLREqual(tr.anchorL.data(), tr.anchorR.data(), tr.length);

        const auto calls = h->callLog();
        // Every setRetriggerPhase on the core is matched by one on the Anchor, same sample, same policy.
        std::size_t corePolicyCalls = 0;
        for (const Call& c : calls) {
            if (c.target != Call::Target::Core || c.kind != Call::Kind::SetRetriggerPhase)
                continue;
            ++corePolicyCalls;
            const auto match = std::count_if(calls.begin(), calls.end(), [&](const Call& o) {
                return o.target == Call::Target::Anchor && o.kind == Call::Kind::SetRetriggerPhase
                       && o.sample == c.sample && o.policy == c.policy;
            });
            CHECK(match == 1);
        }

        std::size_t resets = 0;
        double worstTheta = 0.0;
        for (const PT::NoteOutcomeRecord& r : tr.recorded()) {
            if (r.outcome == PT::NoteOutcome::Release || r.outcome == PT::NoteOutcome::NoAction || r.superseded)
                continue;
            REQUIRE(r.actions.phase == NoteMemory::PhasePolicy::Reset);
            const std::size_t eff = r.effectiveSample;
            REQUIRE(eff != PT::kNoteBehaviourNoSample);
            REQUIRE(eff < tr.length);
            ++resets;
            INFO("note " << n1 << " outcome " << PT::noteOutcomeName(r.outcome) << " event " << r.eventSample
                         << " effective " << eff);

            // The same policy and the noteOn on the same boundary, for both.
            const auto at = [&](Call::Target t, Call::Kind k) {
                return std::count_if(calls.begin(), calls.end(), [&](const Call& c) {
                    return c.target == t && c.kind == k && c.sample == eff
                           && (k != Call::Kind::SetRetriggerPhase || c.policy == NoteMemory::PhasePolicy::Reset);
                });
            };
            CHECK(at(Call::Target::Core, Call::Kind::SetRetriggerPhase) == 1);
            CHECK(at(Call::Target::Anchor, Call::Kind::SetRetriggerPhase) == 1);
            CHECK(at(Call::Target::Core, Call::Kind::NoteOn) == 1);
            CHECK(at(Call::Target::Anchor, Call::Kind::NoteOn) == 1);

            // FR-051: core output exactly 0.0f and Anchor phase 0 at the effective sample.
            CHECK(tr.coreL[eff] == 0.0f);
            CHECK(tr.coreR[eff] == 0.0f);
            CHECK(PT::noteBehaviourAnchorPhaseAt(tr, eff) == 0.0);

            // A = 0: theta = goertzelPhase(core h1) - goertzelPhase(Anchor) over 4 periods after the first second.
            const std::size_t start = eff + PT::noteBehaviourSeconds(1.0, kNbFs);
            const double f = static_cast<double>(tr.fRes[start]);
            REQUIRE(f > 0.0);
            const auto L = static_cast<std::size_t>(std::llround(4.0 * kNbFs / f));
            REQUIRE(start + L <= tr.length);
            const double tc = PT::goertzelPhase(tr.coreL.data() + start, L, kNbFs, f);
            const double ta = PT::goertzelPhase(tr.anchorL.data() + start, L, kNbFs, f);
            const double theta = PT::noteBehaviourWrapRad(tc - ta) * kNbRadToDeg;
            INFO("theta " << theta << " deg");
            CHECK(std::abs(theta) <= kBarDeg);
            worstTheta = std::max(worstTheta, std::abs(theta));
        }
        CHECK(resets == 5);   // FromSilence, Overlap, ReturnToHeld, Tail, FromSilence
        CHECK(corePolicyCalls == resets);
        std::printf("SC-012(a) note %d: %zu Reset noteOns, worst |theta| %.6f deg (bar %.1f)\n", n1, resets,
                    worstTheta, kBarDeg);
    }
}

// =============================================================================
// T020: SC-009(b)(c), SC-011, SC-012(b)-(d), SC-018, SC-026, mutable statics
// =============================================================================

namespace {

constexpr std::array<NoteMemory::Mode, 4> kNbModes{NoteMemory::Mode::AlwaysRetrigger, NoteMemory::Mode::Legato,
                                                   NoteMemory::Mode::EnvelopeRetriggerOnly,
                                                   NoteMemory::Mode::OscillatorContinuous};
constexpr std::array<NoteMemory::Retrigger, 2> kNbRetriggers{NoteMemory::Retrigger::Reset,
                                                             NoteMemory::Retrigger::Continue};
constexpr std::array<GlideMode, 3> kNbGlideModes{GlideMode::ConstantTime, GlideMode::ConstantRate,
                                                 GlideMode::Quantized};
constexpr float kNbMatrixDropA = 12.0f;      // SC-009(b) Drop pass: A > 0 so the Drop is observable
constexpr float kNbMatrixDropMs = 100.0f;
constexpr double kNbPhaseBarDeg = 1.0;       // SC-009(b), SC-012(c)/(d) (Phase 3 SC-003 static bar)
constexpr double kNbDriftBarDeg = 0.5;       // Phase 3 SC-003 drift bar
constexpr std::size_t kNbHostBlock = 37;

AnchorOscillator::Controls nbAnchor(AnchorOscillator::Mode mode) {
    return AnchorOscillator::Controls{.waveform = AnchorOscillator::Waveform::Sine, .mode = mode, .level = 1.0f};
}

std::size_t nbMs(double ms) { return static_cast<std::size_t>(std::llround(ms * kNbFs / 1000.0)); }

/// round(4 fs / f): four periods of f (SC-009(b) Goertzel windows, the SC-012(a) form).
std::size_t nbFourPeriods(float f) {
    return static_cast<std::size_t>(std::llround(4.0 * kNbFs / static_cast<double>(f)));
}

/// L_d = ceil(kDeclickMs fs / 1000) (retrigger_declick.h prepare()).
std::size_t nbDeclickLength() {
    return static_cast<std::size_t>(std::ceil(static_cast<double>(RetriggerDeclick::kDeclickMs) * kNbFs / 1000.0));
}

double nbWrapDeg(double d) {
    d = std::fmod(d, 360.0);
    if (d > 180.0)
        d -= 360.0;
    if (d <= -180.0)
        d += 360.0;
    return d;
}

/// Phase 3 lock window (profundum_anchor_chain_test.cpp:65-67): round(ceil(f0) fs / f0) samples (>= 1 s).
std::size_t nbLockWindowLength(double f0) {
    return static_cast<std::size_t>(std::llround(std::ceil(f0) * kNbFs / f0));
}

/// Start of the lock window centred at t seconds (profundum_anchor_chain_test.cpp:70-75).
std::size_t nbCentredStart(double t, double f0) {
    const auto centre = static_cast<std::size_t>(std::llround(t * kNbFs));
    const std::size_t half = nbLockWindowLength(f0) / 2;
    REQUIRE(centre >= half);
    return centre - half;
}

/// Phase 3 SC-003 delta-theta (profundum_anchor_chain_test.cpp:78-87): wrap(theta_anchor - theta_core) in
/// degrees, both from goertzelPhase at hz over one lock window starting at `start`.
double nbDeltaThetaDeg(const std::vector<float>& anchor, const std::vector<float>& core, std::size_t avail,
                       std::size_t start, double hz) {
    const std::size_t L = nbLockWindowLength(hz);
    REQUIRE(start + L <= avail);
    REQUIRE(start + L <= anchor.size());
    REQUIRE(start + L <= core.size());
    const double ta = PT::goertzelPhase(anchor.data() + start, L, kNbFs, hz);
    const double tc = PT::goertzelPhase(core.data() + start, L, kNbFs, hz);
    return nbWrapDeg((ta - tc) * kNbRadToDeg);
}

bool nbActionsEqual(const NoteMemory::Actions& a, const NoteMemory::Actions& b) {
    return a.oscillatorRestart == b.oscillatorRestart && a.phase == b.phase && a.envelopeRestart == b.envelopeRestart
           && a.dropTrigger == b.dropTrigger && a.dropSnap == b.dropSnap && a.anchorRearm == b.anchorRearm
           && a.declick == b.declick;
}

bool nbIsTransition(const PT::NoteOutcomeRecord& r) {
    return r.outcome != PT::NoteOutcome::Release && r.outcome != PT::NoteOutcome::NoAction;
}

/// h1 goertzelPhase over the first L samples of a fresh, prepared core (the harness's configuration:
/// 64 partials, excitation off, kDefaultControls) given a Reset noteOn at f, rendered in 37-sample blocks.
double nbFreshCoreH1Phase(float f, std::size_t L) {
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(kNbFs, PT::NoteBehaviourHarness::kNumPartials);
    core->setExcitation(ProfundumCore::kDefaultExcitation);
    core->setControls(SpectralShapeRecipe::kDefaultControls);
    core->setRetriggerPhase(ProfundumCore::RetriggerPhase::Reset);
    core->noteOn(f);
    std::vector<float> l(L, 0.0f);
    std::vector<float> r(L, 0.0f);
    for (std::size_t pos = 0; pos < L; pos += kNbHostBlock) {
        const std::size_t n = std::min(kNbHostBlock, L - pos);
        core->processBlock(l.data() + pos, r.data() + pos, n);
    }
    return PT::goertzelPhase(l.data(), L, kNbFs, static_cast<double>(f));
}

/// Renders a script on a fresh harness with the given configuration.
std::unique_ptr<PT::NoteBehaviourHarness> nbRun(const PT::NoteScript& sc, NoteMemory::Mode m, NoteMemory::Retrigger r,
                                                AnchorOscillator::Mode anchorMode, float dropA, float dropMs,
                                                std::size_t hostBlock, PT::NoteBehaviourTraces& tr,
                                                bool useDeclick = true) {
    auto h = makeNbHarness(kNbFs);
    h->setNoteMemory(m, r);
    h->setAnchorControls(nbAnchor(anchorMode));
    h->setDrop(dropA, dropMs, 0.0f, 0.0f);
    h->setUseDeclick(useDeclick);
    PT::noteBehaviourRunScript(*h, sc, hostBlock, tr);
    return h;
}

}  // namespace

// -----------------------------------------------------------------------------
// SC-009(b): the rendered matrix
// -----------------------------------------------------------------------------
// Two renders per (mode, Retrigger) script (MONO, E1, 48 kHz, host block 37, Anchor level 1,
// kDefaultControls):
//   pass 0: LOCK, A = 0   -> Anchor phase, core phase (fresh instance / bitwise / analytic advance), ADSR;
//   pass 1: PUNCH, A = 12 -> Anchor phase, core 0.0f at Reset, ADSR, Drop, re-arm. In LOCK the mode gain
//           is the constant 1 whether it re-arms or not (anchor_oscillator.h:1011-1016, 1033-1036), so the
//           re-arm is observed in PUNCH, whose snap and bridged arms both reach the boost 10^(6/20).
// The Drop is observed through d (the traced DropEnvelope output), the observable of isActive().
TEST_CASE("ProfundumNoteBehaviour_Matrix", "[systems][profundum][note-behaviour]") {
    using Stage = Krate::DSP::ADSRStage;
    const PT::NoteScript sc = PT::noteBehaviourMatrixScript(kNbE1, kNbFs);
    PT::NoteScript refSc = sc;   // the uninterrupted reference: without the same-note Tail
    refSc.events.pop_back();
    refSc.expected.pop_back();
    const std::size_t sameNoteIndex = sc.events.size() - 1;
    const std::size_t dropWindow = nbMs(2.0 * static_cast<double>(kNbMatrixDropMs));
    const std::size_t punchWindow = nbMs(1.0 + 40.0 + 5.0);   // kPunchRiseMs + default punchTimeMs + margin
    const std::size_t ld = nbDeclickLength();

    std::vector<std::pair<std::uint32_t, double>> freshCache;
    const auto freshPhase = [&](float f, std::size_t L) {
        const auto key = std::bit_cast<std::uint32_t>(f);
        for (const auto& kv : freshCache) {
            if (kv.first == key)
                return kv.second;
        }
        const double v = nbFreshCoreH1Phase(f, L);
        freshCache.emplace_back(key, v);
        return v;
    };

    std::size_t nReset = 0;
    std::size_t nSameNote = 0;
    std::size_t nDiffNote = 0;
    double worstResetDeg = 0.0;
    double worstAdvanceDeg = 0.0;
    double worstStep = 0.0;

    for (const auto m : kNbModes) {
        for (const auto r : kNbRetriggers) {
            // Runs only after the plan S8 classification of this script passes (the same comparator).
            requireScriptClassification(sc, m, r, kNbHostBlock, "matrix");

            for (const bool punch : {false, true}) {
                const float A = punch ? kNbMatrixDropA : 0.0f;
                PT::NoteBehaviourTraces tr;
                const auto h = nbRun(sc, m, r, punch ? AnchorOscillator::Mode::Punch : AnchorOscillator::Mode::Lock, A,
                                     kNbMatrixDropMs, kNbHostBlock, tr);
                INFO("mode " << static_cast<int>(m) << " retrigger " << static_cast<int>(r) << " pass "
                             << (punch ? "PUNCH A=12" : "LOCK A=0"));
                REQUIRE(h->contractViolations() == 0);
                REQUIRE(PT::noteOutcomesMatch(sc.expected, tr.recorded()));
                PT::requireLREqual(tr.anchorL.data(), tr.anchorR.data(), tr.length);

                // Anchor phase on FreeRunning: every increment equals f_grid/fs within 1e-9 cycles.
                const PT::NoteBehaviourAnchorAdvance adv =
                    PT::noteBehaviourAnchorAdvance(tr, kNbFs, h->capHz(), 0, tr.length);
                INFO("Anchor worst step error " << adv.worstStep << " cycles at sample " << adv.worstStepAt);
                CHECK(adv.checked > 0);
                CHECK(adv.worstStep <= 1e-9);
                worstStep = std::max(worstStep, adv.worstStep);

                const auto recs = tr.recorded();
                for (std::size_t i = 0; i < recs.size(); ++i) {
                    const PT::NoteOutcomeRecord& rec = recs[i];
                    if (!nbIsTransition(rec))
                        continue;
                    const std::size_t eff = rec.effectiveSample;
                    INFO("event " << i << " " << PT::noteOutcomeName(rec.outcome) << " at " << rec.eventSample
                                  << " effective " << eff);
                    REQUIRE_FALSE(rec.superseded);
                    REQUIRE(eff != PT::kNoteBehaviourNoSample);
                    REQUIRE(eff + dropWindow < tr.length);
                    const NoteMemory::Actions& a = rec.actions;
                    const auto t = static_cast<NoteMemory::Transition>(static_cast<std::uint8_t>(rec.outcome));
                    CHECK(nbActionsEqual(a, NoteMemory::resolve(m, r, t, rec.audible)));
                    CHECK(rec.coreResetBranch == (a.phase == NoteMemory::PhasePolicy::Reset || i == 0));
                    // Restart latency: L_d - 1 on a declicked restart, 0 otherwise.
                    CHECK(eff - rec.eventSample == (a.declick ? ld - 1 : std::size_t{0}));

                    // Anchor phase and core output on Reset.
                    if (rec.coreResetBranch) {
                        CHECK(PT::noteBehaviourAnchorPhaseAt(tr, eff) == 0.0);
                        CHECK(tr.coreL[eff] == 0.0f);
                        CHECK(tr.coreR[eff] == 0.0f);
                    }

                    // Amp envelope: Attack re-entered iff envelopeRestart.
                    CHECK((tr.stage[eff] == static_cast<std::uint8_t>(Stage::Attack)) == a.envelopeRestart);

                    if (!punch) {
                        // Core phase, A = 0.
                        const float fNew = tr.fCore[eff];
                        const std::size_t L2 = nbFourPeriods(fNew);
                        if (rec.coreResetBranch) {
                            const double mine = PT::goertzelPhase(tr.coreL.data() + eff, L2, kNbFs,
                                                                  static_cast<double>(fNew));
                            const double th =
                                PT::noteBehaviourWrapRad(mine - freshPhase(fNew, L2)) * kNbRadToDeg;
                            INFO("Reset: h1 phase vs a fresh instance " << th << " deg");
                            CHECK(std::abs(th) <= kNbPhaseBarDeg);
                            worstResetDeg = std::max(worstResetDeg, std::abs(th));
                            ++nReset;
                        } else if (fNew == tr.fCore[eff - 1]) {
                            // FreeRunning same note: bit-identical to the render without this noteOn.
                            CHECK(i == sameNoteIndex);
                            PT::NoteBehaviourTraces ref;
                            const auto hr = nbRun(refSc, m, r, AnchorOscillator::Mode::Lock, 0.0f, kNbMatrixDropMs,
                                                  kNbHostBlock, ref);
                            REQUIRE(hr->contractViolations() == 0);
                            REQUIRE(PT::noteOutcomesMatch(refSc.expected, ref.recorded()));
                            REQUIRE(ref.length == tr.length);
                            CHECK(PT::samplesBitEqual(tr.coreL.data() + eff, ref.coreL.data() + eff, tr.length - eff));
                            CHECK(PT::samplesBitEqual(tr.coreR.data() + eff, ref.coreR.data() + eff, tr.length - eff));
                            ++nSameNote;
                        } else {
                            // FreeRunning different note: the h1 phase advances by the analytic sum f/fs.
                            const float fOld = tr.fCore[eff - 1];
                            const std::size_t L1 = nbFourPeriods(fOld);
                            REQUIRE(eff >= L1);
                            const std::size_t s1 = eff - L1;
                            const double phi1 =
                                PT::goertzelPhase(tr.coreL.data() + s1, L1, kNbFs, static_cast<double>(fOld));
                            double cycles = 0.0;
                            for (std::size_t n = s1; n < eff; ++n)
                                cycles += static_cast<double>(tr.fCore[n]) / kNbFs;
                            const double phi2 =
                                PT::goertzelPhase(tr.coreL.data() + eff, L2, kNbFs, static_cast<double>(fNew));
                            const double th =
                                PT::noteBehaviourWrapRad(phi2 - (phi1 + PT::kTwoPi * cycles)) * kNbRadToDeg;
                            INFO("FreeRunning " << fOld << " -> " << fNew << " Hz: h1 advance error " << th << " deg");
                            CHECK(std::abs(th) <= kNbPhaseBarDeg);
                            worstAdvanceDeg = std::max(worstAdvanceDeg, std::abs(th));
                            ++nDiffNote;
                        }
                        continue;
                    }

                    // Drop (A = 12): d at the effective sample is A on a snap, the bridge start value on a
                    // bridged trigger (every bridged event of this script starts from an idle Drop, so from
                    // (0, 0)), and the idle 0.0f without a trigger.
                    float peakD = 0.0f;
                    bool idle = true;
                    for (std::size_t n = eff; n < eff + dropWindow; ++n) {
                        peakD = std::max(peakD, tr.d[n]);
                        if (tr.d[n] != 0.0f)
                            idle = false;
                    }
                    if (a.dropTrigger) {
                        CHECK(peakD >= 0.9f * A);
                        if (a.dropSnap) {
                            CHECK(std::abs(tr.d[eff] - A) <= 1e-4f);
                        } else {
                            REQUIRE(eff >= 1);
                            CHECK(tr.d[eff - 1] == 0.0f);
                            CHECK(tr.d[eff] == 0.0f);
                        }
                    } else {
                        CHECK(idle);
                    }

                    // Re-arm (PUNCH): the mode gain restarts (reaches the boost) iff anchorRearm; otherwise it
                    // stays bit-identical to the uninterrupted trace (the rest value held before the event).
                    double peakG = 0.0;
                    for (std::size_t n = eff; n < eff + punchWindow; ++n)
                        peakG = std::max(peakG, tr.modeGain[n]);
                    if (a.anchorRearm) {
                        CHECK(peakG >= 1.5);
                    } else {
                        REQUIRE(eff >= 1);
                        const double rest = tr.modeGain[eff - 1];
                        CHECK(rest == 1.0);
                        bool unchanged = true;
                        for (std::size_t n = eff; n < eff + punchWindow; ++n) {
                            if (std::bit_cast<std::uint64_t>(tr.modeGain[n]) != std::bit_cast<std::uint64_t>(rest))
                                unchanged = false;
                        }
                        CHECK(unchanged);
                    }
                }
            }
        }
    }
    std::printf("SC-009(b): %zu Reset (worst %.6f deg vs fresh), %zu FreeRunning same-note (bitwise), %zu FreeRunning "
                "note changes (worst advance error %.6f deg), worst Anchor step error %.3e cycles\n",
                nReset, worstResetDeg, nSameNote, nDiffNote, worstAdvanceDeg, worstStep);
    CHECK(nReset > 0);
    CHECK(nSameNote > 0);
    CHECK(nDiffNote > 0);
}

// -----------------------------------------------------------------------------
// SC-009(c): POLY voice reuse
// -----------------------------------------------------------------------------
TEST_CASE("ProfundumNoteBehaviour_PolyVoiceReuse", "[systems][profundum][note-behaviour]") {
    using Call = PT::NoteBehaviourCall;
    using Stage = Krate::DSP::ADSRStage;

    VoiceAllocator alloc;
    (void)alloc.setVoiceCount(2);
    alloc.setStealMode(Krate::DSP::StealMode::Hard);
    const auto e1 = alloc.noteOn(40, 100);
    const std::vector<VoiceEvent> ev1(e1.begin(), e1.end());
    const auto e2 = alloc.noteOn(45, 100);
    const std::vector<VoiceEvent> ev2(e2.begin(), e2.end());
    const auto e3 = alloc.noteOn(50, 100);
    const std::vector<VoiceEvent> ev3(e3.begin(), e3.end());
    const auto steal =
        std::find_if(ev3.begin(), ev3.end(), [](const VoiceEvent& e) { return e.type == VoiceEvent::Type::Steal; });
    REQUIRE(steal != ev3.end());
    const std::size_t voice = steal->voiceIndex;
    const std::size_t ld = nbDeclickLength();
    const std::size_t total = PT::noteBehaviourSeconds(1.0, kNbFs);
    const std::array<PT::NoteOutcome, 2> expected{PT::NoteOutcome::FromSilence, PT::NoteOutcome::VoiceReuse};

    for (const auto m : kNbModes) {
        for (const auto r : kNbRetriggers) {
            INFO("mode " << static_cast<int>(m) << " retrigger " << static_cast<int>(r));
            auto h = makeNbHarness(kNbFs);
            h->setNoteMemory(m, r);
            h->setAnchorControls(nbAnchor(AnchorOscillator::Mode::Lock));
            h->setDrop(kNbMatrixDropA, kNbMatrixDropMs, 0.0f, 0.0f);
            PT::NoteBehaviourTraces tr;
            tr.allocate(total);
            h->queueVoiceEvents(ev1, voice, 0);
            h->queueVoiceEvents(ev2, voice, 0);
            h->queueVoiceEvents(ev3, voice, PT::noteBehaviourSeconds(0.5, kNbFs));
            h->render(total, kNbHostBlock, tr);
            REQUIRE(h->contractViolations() == 0);
            REQUIRE(PT::noteOutcomesMatch(expected, tr.recorded()));
            PT::requireLREqual(tr.anchorL.data(), tr.anchorR.data(), tr.length);

            const PT::NoteOutcomeRecord& rec = tr.recorded()[1];
            CHECK(rec.audible);
            CHECK(rec.note == 50);
            const NoteMemory::Actions want = NoteMemory::resolve(m, r, NoteMemory::Transition::VoiceReuse, true);
            CHECK(nbActionsEqual(rec.actions, want));
            const std::size_t eff = rec.effectiveSample;
            REQUIRE(eff != PT::kNoteBehaviourNoSample);
            REQUIRE(eff >= 1);
            REQUIRE(eff + 1 < tr.length);

            // The same policy and the noteOn on the same boundary, for the core and the Anchor.
            const auto calls = h->callLog();
            const auto count = [&](Call::Target target, Call::Kind kind) {
                return std::count_if(calls.begin(), calls.end(), [&](const Call& c) {
                    return c.target == target && c.kind == kind && c.sample == eff
                           && (kind != Call::Kind::SetRetriggerPhase || c.policy == want.phase)
                           && (kind != Call::Kind::NoteOn || target != Call::Target::Anchor
                               || c.rearm == want.anchorRearm);
                });
            };
            CHECK(count(Call::Target::Core, Call::Kind::SetRetriggerPhase) == 1);
            CHECK(count(Call::Target::Anchor, Call::Kind::SetRetriggerPhase) == 1);
            CHECK(count(Call::Target::Core, Call::Kind::NoteOn) == 1);
            CHECK(count(Call::Target::Anchor, Call::Kind::NoteOn) == 1);

            if (want.phase == NoteMemory::PhasePolicy::Reset) {
                CHECK(rec.coreResetBranch);
                CHECK(eff - rec.eventSample == ld - 1);   // declicked: the restart waits for the dip's zero
                CHECK(tr.coreL[eff] == 0.0f);
                CHECK(tr.coreR[eff] == 0.0f);
                CHECK(PT::noteBehaviourAnchorPhaseAt(tr, eff) == 0.0);
            } else {
                CHECK_FALSE(rec.coreResetBranch);
                CHECK(eff == rec.eventSample);
            }
            const PT::NoteBehaviourAnchorAdvance adv =
                PT::noteBehaviourAnchorAdvance(tr, kNbFs, h->capHz(), 0, tr.length);
            INFO("Anchor worst step error " << adv.worstStep << " cycles at " << adv.worstStepAt);
            CHECK(adv.worstStep <= 1e-9);
            CHECK((tr.stage[eff] == static_cast<std::uint8_t>(Stage::Attack)) == want.envelopeRestart);
            if (want.dropTrigger) {
                if (want.dropSnap) {
                    CHECK(std::abs(tr.d[eff] - kNbMatrixDropA) <= 1e-4f);
                } else {
                    CHECK(tr.d[eff - 1] == 0.0f);   // the first Drop (100 ms) is idle by 0.5 s
                    CHECK(tr.d[eff] == 0.0f);       // the bridge starts from rest (0, 0)
                }
            }
        }
    }
}

// -----------------------------------------------------------------------------
// SC-011: declick timing and transparency
// -----------------------------------------------------------------------------
TEST_CASE("ProfundumNoteBehaviour_DeclickTransparency", "[systems][profundum][note-behaviour]") {
    const std::size_t ld = nbDeclickLength();
    const std::size_t latencyBar = ld + 1;   // ceil(kDeclickMs fs / 1000) + 1 (FR-042)
    const PT::NoteScript sc = PT::noteBehaviourStandardScript(kNbE1, kNbFs);

    // out == (core + anchor) * gain * env bitwise on both channels (one gain sample for both); returns the
    // first mismatching sample or kNoteBehaviourNoSample.
    const auto traceIdentity = [](const PT::NoteBehaviourTraces& tr) {
        for (std::size_t n = 0; n < tr.length; ++n) {
            const float sL = tr.coreL[n] + tr.anchorL[n];
            const float sR = tr.coreR[n] + tr.anchorR[n];
            const float eL = sL * tr.gain[n] * tr.env[n];
            const float eR = sR * tr.gain[n] * tr.env[n];
            if (std::bit_cast<std::uint32_t>(eL) != std::bit_cast<std::uint32_t>(tr.L[n])
                || std::bit_cast<std::uint32_t>(eR) != std::bit_cast<std::uint32_t>(tr.R[n]))
                return n;
        }
        return PT::kNoteBehaviourNoSample;
    };

    SECTION("AlwaysRetrigger + Reset: latency, zero at the switch, C1 dip, one gain for both (blocks 1, 37, 512)") {
        for (const std::size_t block : {std::size_t{1}, std::size_t{37}, std::size_t{512}}) {
            INFO("host block " << block);
            PT::NoteBehaviourTraces tr;
            const auto h = nbRun(sc, NoteMemory::Mode::AlwaysRetrigger, NoteMemory::Retrigger::Reset,
                                 AnchorOscillator::Mode::Lock, 0.0f, kNbMatrixDropMs, block, tr);
            REQUIRE(h->contractViolations() == 0);
            REQUIRE(PT::noteOutcomesMatch(sc.expected, tr.recorded()));
            PT::requireLREqual(tr.anchorL.data(), tr.anchorR.data(), tr.length);
            const std::size_t mismatch = traceIdentity(tr);
            INFO("first trace-identity mismatch at " << mismatch);
            CHECK(mismatch == PT::kNoteBehaviourNoSample);

            std::size_t dips = 0;
            for (const PT::NoteOutcomeRecord& rec : tr.recorded()) {
                if (!nbIsTransition(rec) || !rec.actions.declick)
                    continue;
                REQUIRE_FALSE(rec.superseded);
                ++dips;
                const std::size_t ev = rec.eventSample;
                const std::size_t eff = rec.effectiveSample;
                REQUIRE(eff != PT::kNoteBehaviourNoSample);
                REQUIRE(eff < tr.length);
                const std::size_t latency = eff - ev;
                std::printf("SC-011 block %zu %s: onset latency %zu samples (bar %zu)\n", block,
                            PT::noteOutcomeName(rec.outcome), latency, latencyBar);
                INFO(PT::noteOutcomeName(rec.outcome) << " armed at " << ev << " effective " << eff);
                CHECK(latency <= latencyBar);
                CHECK((tr.marks[ev] & PT::NoteBehaviourTraces::kMarkArmed) != 0);
                // Gain, core, Anchor and output exactly 0.0f at the switch (= effective) sample.
                CHECK(tr.gain[eff] == 0.0f);
                CHECK(tr.coreL[eff] == 0.0f);
                CHECK(tr.coreR[eff] == 0.0f);
                CHECK(tr.anchorL[eff] == 0.0f);
                CHECK(tr.anchorR[eff] == 0.0f);
                CHECK(tr.L[eff] == 0.0f);
                CHECK(tr.R[eff] == 0.0f);

                // The dip, from the held 1 at ev - 1 to the 0 at the switch sample, passes the C1 checker
                // with the raised-cosine bound of anchorSegmentD2Bound (g0 = 1, a = -1, len = L_d; C-15).
                constexpr std::size_t kHold = 8;
                REQUIRE(ev >= kHold + 1);
                REQUIRE(latency == ld - 1);
                std::vector<double> g(kHold + ld + 1, 0.0);
                for (std::size_t i = 0; i < g.size(); ++i)
                    g[i] = static_cast<double>(tr.gain[ev - 1 - kHold + i]);
                const PT::AnchorSegmentSpec cosine{PT::AnchorSegmentSpec::Kind::Cosine, 1.0, -1.0, 0.0, 0.0, kHold,
                                                   ld};
                const std::vector<DT::DropC1Segment> segs{
                    DT::DropC1Segment{0, kHold, 0.0}, DT::DropC1Segment{kHold, ld, PT::anchorSegmentD2Bound(cosine)}};
                const DT::DropC1Report rep = DT::dropCheckC1(g, segs, 0.0, 1.0);
                INFO("dip C1: ratio " << rep.worstRatio << " boundary jump " << rep.worstBoundaryJump);
                CHECK(rep.pass);
            }
            CHECK(dips == 3);   // Overlap, ReturnToHeld, Tail (both FromSilence events are inaudible)
        }
    }

    SECTION("a new note during the dip re-targets without a gain step") {
        const PT::NoteScript retarget = PT::noteBehaviourDipRetargetScript(kNbE1, kNbFs);
        PT::NoteScript single = retarget;
        single.events.pop_back();
        single.expected.pop_back();
        PT::NoteBehaviourTraces a;
        PT::NoteBehaviourTraces b;
        const auto ha = nbRun(retarget, NoteMemory::Mode::AlwaysRetrigger, NoteMemory::Retrigger::Reset,
                              AnchorOscillator::Mode::Lock, 0.0f, kNbMatrixDropMs, kNbHostBlock, a);
        const auto hb = nbRun(single, NoteMemory::Mode::AlwaysRetrigger, NoteMemory::Retrigger::Reset,
                              AnchorOscillator::Mode::Lock, 0.0f, kNbMatrixDropMs, kNbHostBlock, b);
        REQUIRE(ha->contractViolations() == 0);
        REQUIRE(hb->contractViolations() == 0);
        REQUIRE(PT::noteOutcomesMatch(retarget.expected, a.recorded()));
        REQUIRE(PT::noteOutcomesMatch(single.expected, b.recorded()));
        REQUIRE(a.length == b.length);
        CHECK(a.recorded()[1].superseded);
        CHECK(a.recorded()[2].effectiveSample == b.recorded()[1].effectiveSample);
        CHECK(PT::samplesBitEqual(a.gain.data(), b.gain.data(), a.length));
        CHECK(traceIdentity(a) == PT::kNoteBehaviourNoSample);
        PT::requireLREqual(a.anchorL.data(), a.anchorR.data(), a.length);
    }

    SECTION("every non-declick transition is bit-identical with and without the declick; latency per cell") {
        for (const auto m : kNbModes) {
            for (const auto r : kNbRetriggers) {
                INFO("mode " << static_cast<int>(m) << " retrigger " << static_cast<int>(r));
                PT::NoteBehaviourTraces with;
                PT::NoteBehaviourTraces without;
                const auto hw =
                    nbRun(sc, m, r, AnchorOscillator::Mode::Lock, 0.0f, kNbMatrixDropMs, kNbHostBlock, with, true);
                const auto ho =
                    nbRun(sc, m, r, AnchorOscillator::Mode::Lock, 0.0f, kNbMatrixDropMs, kNbHostBlock, without, false);
                REQUIRE(hw->contractViolations() == 0);
                REQUIRE(ho->contractViolations() == 0);
                REQUIRE(PT::noteOutcomesMatch(sc.expected, with.recorded()));
                REQUIRE(PT::noteOutcomesMatch(sc.expected, without.recorded()));
                REQUIRE(with.length == without.length);
                PT::requireLREqual(with.anchorL.data(), with.anchorR.data(), with.length);
                PT::requireLREqual(without.anchorL.data(), without.anchorR.data(), without.length);
                CHECK(traceIdentity(with) == PT::kNoteBehaviourNoSample);

                // The two chains can differ only from the first declicked event on (it shifts the restart
                // by L_d - 1 samples): every transition before it, and the whole render when none declicks,
                // is compared bitwise.
                std::size_t upTo = with.length;
                for (const PT::NoteOutcomeRecord& rec : with.recorded()) {
                    if (!nbIsTransition(rec) || !rec.actions.declick)
                        continue;
                    upTo = std::min(upTo, rec.eventSample);
                    const std::size_t latency = rec.effectiveSample - rec.eventSample;
                    std::printf("SC-011 mode %d retrigger %d %s: onset latency %zu samples (bar %zu)\n",
                                static_cast<int>(m), static_cast<int>(r), PT::noteOutcomeName(rec.outcome), latency,
                                latencyBar);
                    CHECK(latency <= latencyBar);
                }
                std::size_t compared = 0;
                for (const PT::NoteOutcomeRecord& rec : with.recorded()) {
                    if (nbIsTransition(rec) && !rec.actions.declick && rec.effectiveSample < upTo)
                        ++compared;
                }
                INFO("compared up to sample " << upTo << ", " << compared << " non-declick transition(s)");
                CHECK(compared >= 1);
                CHECK(PT::samplesBitEqual(with.L.data(), without.L.data(), upTo));
                CHECK(PT::samplesBitEqual(with.R.data(), without.R.data(), upTo));
                if (r == NoteMemory::Retrigger::Continue)
                    CHECK(upTo == with.length);   // Continue never declicks: the whole render is compared
            }
        }
    }
}

// -----------------------------------------------------------------------------
// SC-012(b)-(d): routing
// -----------------------------------------------------------------------------
TEST_CASE("ProfundumNoteBehaviour_Routing", "[systems][profundum][note-behaviour]") {
    constexpr std::array<int, 2> kNotes{kNbE1, kNbC3};
    constexpr std::array<float, 2> kAmounts{12.0f, 48.0f};
    constexpr std::array<float, 2> kTimes{60.0f, 200.0f};

    SECTION("(b) LOCK under a Drop: the Anchor stays on f_res, the core on the commanded staircase") {
        for (const int note : kNotes) {
            for (const float A : kAmounts) {
                for (const float T : kTimes) {
                    INFO("note " << note << " A " << A << " T " << T << " ms");
                    auto h = makeNbHarness(kNbFs);
                    h->setAnchorControls(nbAnchor(AnchorOscillator::Mode::Lock));
                    h->setDrop(A, T, 0.0f, 0.0f);
                    const std::size_t total = nbMs(3.0 * static_cast<double>(T) + 100.0);
                    PT::NoteBehaviourTraces tr;
                    tr.allocate(total);
                    h->noteOn(note, 100, 0);
                    h->render(total, kNbHostBlock, tr);
                    REQUIRE(h->contractViolations() == 0);
                    REQUIRE(tr.numOutcomes == 1);
                    REQUIRE(tr.recorded()[0].effectiveSample == 0);
                    const auto end = static_cast<std::size_t>(std::ceil(h->drop().plannedCurve().endSample));
                    REQUIRE(end > 0);
                    REQUIRE(end < tr.length);

                    const float cap = h->capHz();
                    double worstAnchorCents = 0.0;
                    double worstCoreCents = 0.0;
                    double worstStairCents = 0.0;
                    std::size_t updates = 0;
                    for (std::size_t n = 1; n <= end; ++n) {
                        double dphi = tr.anchorPhase[n] - tr.anchorPhase[n - 1];
                        dphi -= std::floor(dphi + 0.5);
                        const double inst = dphi * kNbFs;
                        worstAnchorCents = std::max(
                            worstAnchorCents, std::abs(1200.0 * std::log2(inst / static_cast<double>(tr.fRes[n]))));
                        if (n % ProfundumCore::kPitchUpdateInterval == 0) {   // the grid restarts at the noteOn (0)
                            const float target = std::clamp(tr.fCore[n], ProfundumCore::kMinF0Hz, cap);
                            worstCoreCents =
                                std::max(worstCoreCents, std::abs(1200.0 * std::log2(static_cast<double>(tr.coreFreq[n])
                                                                                     / static_cast<double>(target))));
                            ++updates;
                        }
                        worstStairCents =
                            std::max(worstStairCents, std::abs(1200.0 * std::log2(static_cast<double>(tr.coreFreq[n])
                                                                                  / static_cast<double>(tr.fCore[n]))));
                    }
                    std::printf("SC-012(b) note %d A %.0f T %.0f ms: Anchor vs f_res worst %.6f cents (bar 1), core "
                                "update vs commanded worst %.6f cents over %zu updates (bar 0.1), staircase error "
                                "%.3f cents (recorded)\n",
                                note, static_cast<double>(A), static_cast<double>(T), worstAnchorCents, worstCoreCents,
                                updates, worstStairCents);
                    INFO("staircase error " << worstStairCents << " cents (recorded, not gated)");
                    CHECK(worstAnchorCents <= 1.0);
                    CHECK(worstCoreCents <= 0.1);
                    CHECK(updates > 0);
                }
            }
        }
    }

    SECTION("(c) FOLLOW r = 1 under a Drop: the Anchor integrates the core's post-Drop f0") {
        for (const int note : kNotes) {
            for (const float A : kAmounts) {
                for (const float T : kTimes) {
                    INFO("note " << note << " A " << A << " T " << T << " ms");
                    auto h = makeNbHarness(kNbFs);
                    h->setAnchorControls(nbAnchor(AnchorOscillator::Mode::Follow));
                    h->setDrop(A, T, 0.0f, 0.0f);
                    const std::size_t total = nbMs(3.0 * static_cast<double>(T) + 1800.0);
                    PT::NoteBehaviourTraces tr;
                    tr.allocate(total);
                    h->noteOn(note, 100, 0);
                    h->render(total, kNbHostBlock, tr);
                    REQUIRE(h->contractViolations() == 0);
                    REQUIRE(tr.numOutcomes == 1);
                    const auto end = static_cast<std::size_t>(std::ceil(h->drop().plannedCurve().endSample));
                    REQUIRE(end > 0);
                    REQUIRE(end < tr.length);

                    // FR-058: in FOLLOW the lock is bypassed (d_c == d bitwise) and the Anchor is fed f_core.
                    CHECK(PT::samplesBitEqual(tr.dc.data(), tr.d.data(), tr.length));
                    CHECK(PT::samplesBitEqual(tr.fAnchor.data(), tr.fCore.data(), tr.length));

                    // The accumulated phase() advance over the Drop equals the integral of the fed trajectory on
                    // the Phase 3 cadence within 1e-6 cycles at every sample.
                    const PT::NoteBehaviourAnchorAdvance adv =
                        PT::noteBehaviourAnchorAdvance(tr, kNbFs, h->capHz(), 1, end + 1);
                    // Residual against the core's rendered pitch, recorded.
                    double residual = 0.0;
                    double worstResidual = 0.0;
                    for (std::size_t n = 0; n <= end; ++n) {
                        residual += (static_cast<double>(tr.fAnchor[n]) - static_cast<double>(tr.coreFreq[n])) / kNbFs;
                        worstResidual = std::max(worstResidual, std::abs(residual));
                    }
                    // After t_s: Phase 3 SC-003's 1 s window, centred 1 s after the landing.
                    const double hz = static_cast<double>(tr.fRes[end]);
                    const double tEnd = static_cast<double>(end) / kNbFs;
                    const double dTheta =
                        nbDeltaThetaDeg(tr.anchorL, tr.coreL, tr.length, nbCentredStart(tEnd + 1.0, hz), hz);
                    std::printf("SC-012(c) note %d A %.0f T %.0f ms: accumulated phase error %.3e cycles at %zu (bar "
                                "1e-6), worst step %.3e, residual vs currentFrequency() %.3e cycles (recorded), "
                                "dTheta(t_s + 1 s) %+.6f deg (bar 1)\n",
                                note, static_cast<double>(A), static_cast<double>(T), adv.worstAccumulated,
                                adv.worstAccumulatedAt, adv.worstStep, worstResidual, dTheta);
                    CHECK(adv.checked == end);
                    CHECK(adv.worstAccumulated <= 1e-6);
                    CHECK(std::abs(dTheta) <= kNbPhaseBarDeg);
                }
            }
        }
    }

    SECTION("(d) glide in each of the three modes, no Drop: the lock holds after the glide") {
        const auto s = [](double sec) { return PT::noteBehaviourSeconds(sec, kNbFs); };
        for (const int note : kNotes) {
            for (const GlideMode g : kNbGlideModes) {
                INFO("note " << note << " glide mode " << static_cast<int>(g));
                auto h = makeNbHarness(kNbFs);
                h->setNoteMemory(NoteMemory::Mode::Legato, NoteMemory::Retrigger::Continue);   // no restarts
                h->setAnchorControls(nbAnchor(AnchorOscillator::Mode::Lock));
                h->mono().setPortamentoTime(200.0f);
                h->mono().setGlideMode(g);
                const std::size_t total = s(5.5);
                PT::NoteBehaviourTraces tr;
                tr.allocate(total);
                h->noteOn(note, 100, 0);
                h->noteOn(note + 7, 100, s(1.5));   // Overlap: glide up 7 st
                h->noteOff(note + 7, s(3.5));       // ReturnToHeld: glide back down
                h->render(total, kNbHostBlock, tr);
                REQUIRE(h->contractViolations() == 0);
                const std::array<PT::NoteOutcome, 3> expected{PT::NoteOutcome::FromSilence, PT::NoteOutcome::Overlap,
                                                              PT::NoteOutcome::ReturnToHeld};
                REQUIRE(PT::noteOutcomesMatch(expected, tr.recorded()));
                // The glides moved the pitch and returned it.
                REQUIRE(tr.fRes[s(1.8)] > 1.4f * tr.fRes[s(1.4)]);
                REQUIRE(std::abs(tr.fRes[s(3.8)] / tr.fRes[s(1.4)] - 1.0f) <= 1e-6f);

                const double hz0 = static_cast<double>(tr.fRes[s(1.0)]);
                const double hzUp = static_cast<double>(tr.fRes[s(1.8)]);
                const double d0 = nbDeltaThetaDeg(tr.anchorL, tr.coreL, tr.length, nbCentredStart(1.0, hz0), hz0);
                const double dUp = nbDeltaThetaDeg(tr.anchorL, tr.coreL, tr.length, s(1.8), hzUp);   // glide end + 100 ms
                const double dDown = nbDeltaThetaDeg(tr.anchorL, tr.coreL, tr.length, s(3.8), hz0);
                const double drift = nbWrapDeg(dDown - d0);
                std::printf("SC-012(d) note %d glide %d: dTheta(1 s) %+.6f, after up-glide %+.6f, after down-glide "
                            "%+.6f deg (bar 1), drift %+.6f deg (bar 0.5)\n",
                            note, static_cast<int>(g), d0, dUp, dDown, drift);
                CHECK(std::abs(d0) <= kNbPhaseBarDeg);
                CHECK(std::abs(dUp) <= kNbPhaseBarDeg);
                CHECK(std::abs(dDown) <= kNbPhaseBarDeg);
                CHECK(std::abs(drift) <= kNbDriftBarDeg);
            }
        }
    }
}

// SC-012(d) 60 s window: |dTheta(60 s) - dTheta(1 s)| <= 0.5 deg with 200 ms +-7 st glides every 5 s, E1 and
// C3, 48 kHz, the three glide modes. Rendered in 1 s chunks through one reused trace buffer (a 61 s trace set
// would be ~240 MB per render).
TEST_CASE("ProfundumNoteBehaviour_GlideLock60s", "[systems][profundum][note-behaviour][long]") {
    const auto s = [](double sec) { return PT::noteBehaviourSeconds(sec, kNbFs); };
    const std::size_t total = s(61.0);
    const std::size_t chunk = s(1.0);
    constexpr int kGlides = 11;   // at 5, 10, ..., 55 s: odd ones up (Overlap), even ones down (ReturnToHeld)

    for (const int note : {kNbE1, kNbC3}) {
        for (const GlideMode g : kNbGlideModes) {
            INFO("note " << note << " glide mode " << static_cast<int>(g));
            auto h = makeNbHarness(kNbFs);
            h->setNoteMemory(NoteMemory::Mode::Legato, NoteMemory::Retrigger::Continue);
            h->setAnchorControls(nbAnchor(AnchorOscillator::Mode::Lock));
            h->mono().setPortamentoTime(200.0f);
            h->mono().setGlideMode(g);
            std::vector<PT::NoteOutcome> expected{PT::NoteOutcome::FromSilence};
            h->noteOn(note, 100, 0);
            for (int k = 1; k <= kGlides; ++k) {
                const std::size_t at = s(5.0 * k);
                if (k % 2 == 1) {
                    h->noteOn(note + 7, 100, at);
                    expected.push_back(PT::NoteOutcome::Overlap);
                } else {
                    h->noteOff(note + 7, at);
                    expected.push_back(PT::NoteOutcome::ReturnToHeld);
                }
            }

            PT::NoteBehaviourTraces tr;
            tr.allocate(chunk);
            std::vector<float> core(total, 0.0f);
            std::vector<float> anchor(total, 0.0f);
            std::vector<float> fRes(total, 0.0f);
            for (std::size_t pos = 0; pos < total; pos += chunk) {
                const std::size_t n = std::min(chunk, total - pos);
                tr.length = 0;
                h->render(n, kNbHostBlock, tr);
                REQUIRE(tr.length == n);
                const auto at = static_cast<std::ptrdiff_t>(pos);
                std::copy_n(tr.coreL.begin(), n, core.begin() + at);
                std::copy_n(tr.anchorL.begin(), n, anchor.begin() + at);
                std::copy_n(tr.fRes.begin(), n, fRes.begin() + at);
            }
            REQUIRE(h->contractViolations() == 0);
            REQUIRE(PT::noteOutcomesMatch(expected, tr.recorded()));

            const double hzLow = static_cast<double>(fRes[s(1.0)]);
            const double d1 = nbDeltaThetaDeg(anchor, core, total, nbCentredStart(1.0, hzLow), hzLow);
            CHECK(std::abs(d1) <= kNbPhaseBarDeg);
            double refUp = 0.0;
            double worstDrift = 0.0;
            for (int k = 1; k <= kGlides; ++k) {
                const std::size_t start = s(5.0 * k + 0.3);   // 100 ms after the 200 ms glide ends
                const double hz = static_cast<double>(fRes[start]);
                const double d = nbDeltaThetaDeg(anchor, core, total, start, hz);
                CHECK(std::abs(d) <= kNbPhaseBarDeg);
                if (k == 1)
                    refUp = d;
                const double drift = nbWrapDeg(d - (k % 2 == 1 ? refUp : d1));   // against the same note's first
                worstDrift = std::max(worstDrift, std::abs(drift));
                CHECK(std::abs(drift) <= kNbDriftBarDeg);
            }
            const double hz60 = static_cast<double>(fRes[s(60.0)]);   // the up note after the 11th glide
            const double d60 = nbDeltaThetaDeg(anchor, core, total, nbCentredStart(60.0, hz60), hz60);
            const double drift60 = nbWrapDeg(d60 - refUp);
            worstDrift = std::max(worstDrift, std::abs(drift60));
            std::printf("SC-012(d) 60 s note %d glide %d: dTheta(1 s) %+.6f deg, dTheta(60 s) %+.6f deg, drift %+.6f "
                        "deg, worst drift %.6f deg (bar 0.5)\n",
                        note, static_cast<int>(g), d1, d60, drift60, worstDrift);
            CHECK(std::abs(drift60) <= kNbDriftBarDeg);
        }
    }
}

// -----------------------------------------------------------------------------
// SC-018: no allocation
// -----------------------------------------------------------------------------
TEST_CASE("ProfundumNoteBehaviour_NoAllocation", "[systems][profundum][note-behaviour]") {
    // Everything that allocates (harness, traces, standalone components) is built outside the scope.
    auto h = makeNbHarness(kNbFs);
    h->setNoteMemory(NoteMemory::Mode::AlwaysRetrigger, NoteMemory::Retrigger::Reset);
    h->setAnchorControls(nbAnchor(AnchorOscillator::Mode::Punch));
    h->setDrop(24.0f, 60.0f, 1.0f, 1.0f);
    h->mono().setPortamentoTime(50.0f);
    // The three renders below total 0.5 + 1.5 + 1.5 = 3.5 s; the traces must hold all of it (a shorter
    // trace counts one harness contract violation per overflowing sub-block).
    const std::size_t total = PT::noteBehaviourSeconds(3.5, kNbFs);
    PT::NoteBehaviourTraces tr;
    tr.allocate(total);

    MonoHandler mono;
    mono.prepare(kNbFs);
    mono.setPortamentoTime(500.0f);
    NoteMemory memory;
    DropEnvelope env;
    env.prepare(kNbFs);
    DropPhaseLock lock;
    lock.prepare(kNbFs);
    RetriggerDeclick declick;
    declick.prepare(kNbFs);
    std::array<float, 64> bufA{};
    std::array<float, 64> bufB{};
    const float cap = SpectralShapeRecipe::capFrequency(kNbFs);
    float sink = 0.0f;
    int resolved = 0;

    std::size_t observed = 0;
    {
        [[maybe_unused]] const TestHelpers::AllocationScope scope;

        // Harness events and renders: snap, declicked Reset restarts, release, tail, voice reuse (host
        // block 1 for the first 0.5 s, then 37), then FreeRunning bridged restarts (block 512).
        h->noteOn(kNbE1, 100, 0);
        h->noteOn(kNbE1 + 5, 100, 12000);
        h->noteOff(kNbE1 + 5, 48000);
        h->noteOff(kNbE1, 60000);
        h->noteOn(kNbE1 + 3, 100, 62000);
        h->voiceReuse(kNbE1 + 7, 100, 90000);
        h->render(PT::noteBehaviourSeconds(0.5, kNbFs), 1, tr);
        h->render(PT::noteBehaviourSeconds(1.5, kNbFs), kNbHostBlock, tr);
        h->setNoteMemory(NoteMemory::Mode::OscillatorContinuous, NoteMemory::Retrigger::Continue);
        h->noteOn(kNbE1 + 2, 100, 100);
        h->noteOff(kNbE1 + 2, 9000);
        h->noteOn(kNbE1 + 9, 100, 9100);
        h->render(PT::noteBehaviourSeconds(1.5, kNbFs), 512, tr);

        // MonoHandler: every ordered pair of glide modes, switched mid-glide.
        (void)mono.noteOn(40, 100);
        for (const GlideMode from : kNbGlideModes) {
            for (const GlideMode to : kNbGlideModes) {
                if (from == to)
                    continue;
                mono.setGlideMode(from);
                (void)mono.noteOn(52, 100);   // glide up
                for (int i = 0; i < 64; ++i)
                    sink += mono.processPortamento();
                mono.setGlideMode(to);        // mid-glide switch
                for (int i = 0; i < 64; ++i)
                    sink += mono.processPortamento();
                (void)mono.noteOff(52);       // glide back to the held 40
                for (int i = 0; i < 64; ++i)
                    sink += mono.processPortamento();
            }
        }

        // NoteMemory: every setter and resolve.
        for (const auto m : kNbModes) {
            memory.setMode(m);
            for (const auto r : kNbRetriggers) {
                memory.setRetrigger(r);
                for (std::uint8_t t = 0; t < 5; ++t) {
                    for (const bool audible : {false, true}) {
                        const NoteMemory::Actions a = memory.resolve(static_cast<NoteMemory::Transition>(t), audible);
                        resolved += a.declick ? 2 : 1;
                    }
                }
            }
        }

        // DropEnvelope: every setter, trigger(true / false), process / processBlock.
        env.setAmount(48.0f);
        env.setTime(1.0f);
        env.setShape(1.0f);
        env.setRebound(1.0f);
        env.trigger(true);
        sink += env.process();
        env.processBlock(bufA.data(), bufA.size());
        env.setTime(200.0f);
        env.trigger(false);
        // DropPhaseLock: trigger + the solving process, processBlock, setBypass.
        lock.trigger(env.plannedCurve(), 41.2f, 3, ProfundumCore::kMinF0Hz, cap, DropPhaseLock::TriggerKind::Bridged);
        sink += lock.process(env.process());
        env.processBlock(bufA.data(), bufA.size());
        lock.processBlock(bufA.data(), bufB.data(), bufB.size());
        env.trigger(true);
        lock.trigger(env.plannedCurve(), 41.2f, 0, ProfundumCore::kMinF0Hz, cap, DropPhaseLock::TriggerKind::SnapReset);
        env.processBlock(bufA.data(), bufA.size());
        lock.processBlock(bufA.data(), bufB.data(), bufB.size());
        lock.setBypass(true);
        sink += lock.process(env.process());
        lock.setBypass(false);
        // RetriggerDeclick: arm, process, processBlock.
        declick.arm();
        sink += declick.process();
        declick.processBlock(bufA.data(), bufA.size());
        declick.arm();
        declick.processBlock(bufB.data(), bufB.size());

        // AllocationScope latches in its destructor (allocation_detector.h:117-119): read the live count.
        observed = TestHelpers::AllocationDetector::instance().getAllocationCount();
    }
    sink += bufA[0] + bufB[0];
    INFO("allocations observed: " << observed << ", harness outcomes " << tr.numOutcomes << ", resolves " << resolved);
    CHECK(h->contractViolations() == 0);
    CHECK(tr.numOutcomes >= 8);
    CHECK(resolved > 0);
    CHECK(Krate::DSP::detail::isFinite(sink));
    REQUIRE(observed == 0u);

    // Positive control: the detector sees an allocation inside an identical scope.
    std::size_t control = 0;
    int probeObserved = 0;
    {
        [[maybe_unused]] const TestHelpers::AllocationScope scope;
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        int* probe = new int(42);
        volatile int* probeSink = probe;   // defeat new/delete elision
        probeObserved = *probeSink;
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        delete probe;
        control = TestHelpers::AllocationDetector::instance().getAllocationCount();
    }
    CHECK(probeObserved == 42);
    CHECK(control >= 1u);
}

// -----------------------------------------------------------------------------
// SC-026: the Anchor is untouched by the lock in LOCK, PUNCH and GHOST
// -----------------------------------------------------------------------------
TEST_CASE("ProfundumNoteBehaviour_AnchorUntouched", "[systems][profundum][note-behaviour]") {
    using K = PT::NoteScriptEvent::Kind;
    PT::NoteScript sc;   // A = 48, T = 60 ms, elastic (shape 1), R = 1; a bridged retrigger mid-fall
    sc.events = {{0, kNbE1, 100, K::On}, {nbMs(30.0), kNbE1 + 5, 100, K::On}};
    sc.expected = {PT::NoteOutcome::FromSilence, PT::NoteOutcome::Overlap};
    sc.length = PT::noteBehaviourSeconds(0.6, kNbFs);

    for (const auto mode : {AnchorOscillator::Mode::Lock, AnchorOscillator::Mode::Punch, AnchorOscillator::Mode::Ghost}) {
        INFO("anchor mode " << static_cast<int>(mode));
        PT::NoteBehaviourTraces on;
        PT::NoteBehaviourTraces off;
        const std::array<PT::NoteBehaviourTraces*, 2> traces{&on, &off};
        for (std::size_t k = 0; k < 2; ++k) {
            auto h = makeNbHarness(kNbFs);
            h->setNoteMemory(NoteMemory::Mode::AlwaysRetrigger, NoteMemory::Retrigger::Continue);   // bridged
            h->setAnchorControls(nbAnchor(mode));
            h->setDrop(48.0f, 60.0f, 1.0f, 1.0f);
            h->setLockBypass(k == 1);
            PT::noteBehaviourRunScript(*h, sc, kNbHostBlock, *traces[k]);
            REQUIRE(h->contractViolations() == 0);
            REQUIRE(PT::noteOutcomesMatch(sc.expected, traces[k]->recorded()));
            const PT::NoteOutcomeRecord& rec = traces[k]->recorded()[1];
            REQUIRE(rec.lockTriggered);
            REQUIRE(rec.lockKind == DropPhaseLock::TriggerKind::Bridged);
        }
        // Non-vacuous: the lock corrected the core's offset when enabled and was exactly off when bypassed.
        CHECK_FALSE(PT::samplesBitEqual(on.dc.data(), on.d.data(), on.length));
        CHECK(PT::samplesBitEqual(off.dc.data(), off.d.data(), off.length));

        // Ruling (A) (spec Clarifications 2026-10-13 "T023 epoch 6 readings"): the Anchor's pitch, phase() and
        // mode gain are bit-identical; its output follows the core's delivered h1 (deviation 6: a1 =
        // deliveredGains()[0], renormalised per f by applyMask). Gate: output_on == output_off * (a1_on / a1_off)
        // per sample within float rounding (<= -120 dB re the Anchor's peak); the raw output difference is
        // recorded, not gated.
        REQUIRE(on.length == off.length);
        CHECK(std::memcmp(on.fAnchor.data(), off.fAnchor.data(), on.length * sizeof(float)) == 0);
        CHECK(std::memcmp(on.anchorInc.data(), off.anchorInc.data(), on.length * sizeof(double)) == 0);
        CHECK(std::memcmp(on.anchorPhase.data(), off.anchorPhase.data(), on.length * sizeof(double)) == 0);
        CHECK(std::memcmp(on.modeGain.data(), off.modeGain.data(), on.length * sizeof(double)) == 0);
        const PT::NoteBehaviourAnchorDiff diff = PT::noteBehaviourAnchorOutputDiff(on, off, on.length);
        std::printf("SC-026 AnchorUntouched mode %d: Anchor output a1-scaled max |diff| %.3e = %.2f dB re peak %.4f "
                    "(bar %.0f); raw max |diff| %.3e = %.2f dB (recorded); unchecked %zu\n",
                    static_cast<int>(mode), diff.scaledMaxDiff, diff.scaledDb, diff.peak,
                    PT::kNoteBehaviourAnchorScaledBarDb, diff.maxDiff, diff.db, diff.unchecked);
        CHECK(diff.peak > 0.0);   // non-vacuous: the Anchor sounded
        CHECK(diff.scaledDb <= PT::kNoteBehaviourAnchorScaledBarDb);
        PT::requireLREqual(on.anchorL.data(), on.anchorR.data(), on.length);
    }
}

// -----------------------------------------------------------------------------
// Plan S9: no mutable statics in the five new headers
// -----------------------------------------------------------------------------
TEST_CASE("ProfundumNoteBehaviour_NoMutableStatics", "[systems][profundum][note-behaviour]") {
    constexpr std::array<const char*, 5> kHeaders{"core/drop_curve.h", "primitives/retrigger_declick.h",
                                                  "processors/drop_envelope.h", "processors/drop_phase_lock.h",
                                                  "processors/note_memory.h"};
    for (const char* rel : kHeaders) {
        const std::string path = std::string(KRATE_DSP_TESTS_DIR) + "/../include/krate/dsp/" + rel;
        std::ifstream in(path, std::ios::binary);
        INFO("header " << path);
        REQUIRE(in.is_open());
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        REQUIRE_FALSE(text.empty());
        const std::vector<std::string> hits = PT::anchorFindMutableStatics(text);
        for (const std::string& hit : hits)
            std::printf("Mutable static in %s: %s\n", rel, hit.c_str());
        std::printf("Plan S9 source scan %s: %zu bytes, %zu mutable static(s) (bar 0)\n", rel, text.size(),
                    hits.size());
        CHECK(hits.empty());
    }
}
