// ==============================================================================
// Profundum Phase 9 (specs/profundum-phase9-voice-engine) - profundum_voice_test.cpp
// ==============================================================================
// Will hold: SC-019, SC-020, SC-033, SC-034, FR-010 / FR-015.
// Filled by: T007 (the direct-voice cases below, tasks.md deviation 2), T010 (the engine-driven voice cases).
//
// T007 drives ProfundumVoice directly through its own public API: prepare(48000.0, A_s), a test-side
// BassStereoField prepared at fs / 32 with advanceControl() once per interval, a hand-built IntervalInputs
// ({ .state, .coreControls = s.recipe copy, .foldAmount, .anchorLevel, .field, .forceLinearCore = false }), an
// interval f_res array held at the note frequency, and a test observer attached through setObserverSlot that copies
// every onVoiceChunk span and every onCall record into preallocated vectors.
//   ProfundumVoice_PhasePolicyMapping   FR-010: the three phase enums agree on Reset == 0, FreeRunning == 1
//   ProfundumVoice_OutputIdentity       FR-015 / SC-019(b): sum == (c*gAmp)*gD + (a*gAmp)*gD bitwise, a dip included
//   ProfundumVoice_FeedSkipAndRamp      FR-003 / C-10: g = 0 skips the feed (core and Character taps == detached);
//                                       the 0 -> 1 interval ramps as S9.5 writes it, landing on 1.0f
//   ProfundumVoice_A1MatchesUnbiasedH1  FR-005 / C-6: a1 == MassDynamicsTest::unbiasedH1Amplitude bitwise
//   ProfundumVoice_ChangeOnlySetters    FR-013 / C-20: the first interval issues every step, an unchanged one none
//   ProfundumVoice_MeterBounded         FR-008 / C-5: f_res above fs/2: meter theta in [0, 1), f == capHz, output finite
//   ProfundumVoice_IdleNotActive        FR-015: idle after the release ends; active while a dip is pending
// T010 drives the same voice through ProfundumEngine (tasks.md deviation 2) with ProfundumTest::voiceEngineRender
// (profundum_voice_engine_test_helpers.h) or, where the bend range must differ from the default, a TU-local copy of
// that driver that also calls setPitchBendRange before prepare (veRenderWithBendRange; the driver has no such event):
//   VoiceEngine_AnchorJoin              SC-019 (a)-(d): detached == attached at g = 0; Sum == the FR-015 identity over
//                                       every rendering voice, with a Reset retrigger and a POLY steal; Anchor tap
//                                       independent of g; the feed ramp read through the engine
//   VoiceEngine_DropRouting             SC-020 / FR-006: core and Anchor pitch taps == a test-side MonoHandler +
//                                       NoteProcessor + DropEnvelope + DropPhaseLock reference, bitwise (C-11)
//   VoiceEngine_DropLatchedAtTrigger    FR-006: a mid-Drop morph does not touch the sounding Drop; the next note does
//   VoiceEngine_RectifySymmetry         SC-033 / FR-008: the HalfWave flag at -30 / -25 / -15 / -10 dB, hysteresis at
//                                       -20 dB, SyncPhase records and the core h1 phase at each Reset-branch sample
//   VoiceEngine_PitchBend               SC-034 / FR-024: a full bend moves f_res by the range within 0.1 cent
//   VoiceEngine_MeterBoundedAtTopNote   FR-008 / C-5: note 127 + bend 24 st through the engine
// Sum buffers are cleared to -0.0f before every chunk: -0 is the IEEE additive identity (-0 + x == x for every x,
// x == -0 included), so the sum buffer holds the voice's contribution bit for bit (a +0 clear would turn a -0
// contribution into +0).
// ==============================================================================

#include <krate/dsp/systems/profundum_engine.h>
#include <krate/dsp/systems/profundum_engine_observer.h>
#include <krate/dsp/systems/profundum_macro_matrix.h>
#include <krate/dsp/systems/profundum_motion.h>
#include <krate/dsp/systems/profundum_voice.h>

#include "bass_morph_state_helpers.h"
#include "mass_dynamics_test_helpers.h"
#include "profundum_voice_engine_test_helpers.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/midi_utils.h>
#include <krate/dsp/core/pitch_utils.h>
#include <krate/dsp/primitives/retrigger_declick.h>
#include <krate/dsp/processors/drop_envelope.h>
#include <krate/dsp/processors/drop_phase_lock.h>
#include <krate/dsp/processors/mono_handler.h>
#include <krate/dsp/processors/note_processor.h>
#include <krate/dsp/processors/note_memory.h>
#include <krate/dsp/processors/spectral_shape_recipe.h>
#include <krate/dsp/systems/anchor_oscillator.h>
#include <krate/dsp/systems/bass_stereo_field.h>
#include <krate/dsp/systems/morph_state.h>
#include <krate/dsp/systems/profundum_core.h>

#include <catch2/catch_test_macros.hpp>

#include <enable_ftz_daz.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

namespace {

using Krate::DSP::AnchorOscillator;
using Krate::DSP::BassStereoField;
using Krate::DSP::MorphState;
using Krate::DSP::NoteMemory;
using Krate::DSP::ProfundumCall;
using Krate::DSP::ProfundumCallRecord;
using Krate::DSP::ProfundumCore;
using Krate::DSP::ProfundumEngineObserver;
using Krate::DSP::ProfundumEngineSeams;
using Krate::DSP::ProfundumVoice;
using Krate::DSP::ProfundumVoiceTaps;
using Krate::DSP::SpectralShapeRecipe;

constexpr double kVeFs = 48000.0;
constexpr std::size_t kVeInterval = ProfundumVoice::kControlInterval;
constexpr int kVeNoteE1 = 28;
constexpr int kVeNoteA1 = 33;
constexpr int kVeNoteC3 = 48;

[[nodiscard]] bool veSameBits(float a, float b) noexcept {
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

/// Plan S1 C-8: base "A" with ampEnv.sustain = 1 (A_s).
[[nodiscard]] MorphState veStateAs() {
    MorphState s = Krate::DSP::BassMorphTest::showcasePair().first;
    s.ampEnv.sustain = 1.0f;
    return s;
}

/// Copies every voice chunk (per-sample spans concatenated) and every call record. Storage is reserved up front;
/// the callbacks only append.
class VeObserver final : public ProfundumEngineObserver {
public:
    struct ChunkScalars {
        std::uint64_t sample = 0;
        std::size_t offset = 0;   // index of the chunk's first sample in the per-sample vectors
        std::size_t n = 0;
        float followRatio = 0.0f;
        float a1 = 0.0f;
        float anchorLevel = 0.0f;
        float feedFrom = 0.0f;
        float feedTo = 0.0f;
        float meterTheta = 0.0f;
        float meterFreqHz = 0.0f;
        bool halfWave = false;
    };

    void reserve(std::size_t samples, std::size_t chunks, std::size_t calls) {
        for (std::vector<float>* v : perSample())
            v->reserve(samples);
        chunks_.reserve(chunks);
        calls_.reserve(calls);
    }
    void clear() noexcept {
        for (std::vector<float>* v : perSample())
            v->clear();
        chunks_.clear();
        calls_.clear();
    }

    void onCall(const ProfundumCallRecord& r) noexcept override { calls_.push_back(r); }
    void onVoiceChunk(const ProfundumVoiceTaps& t) noexcept override {
        ChunkScalars c{};
        c.sample = t.sample;
        c.offset = coreL.size();
        c.n = t.n;
        c.followRatio = t.followRatio;
        c.a1 = t.a1;
        c.anchorLevel = t.anchorLevel;
        c.feedFrom = t.feedFrom;
        c.feedTo = t.feedTo;
        c.meterTheta = t.meterTheta;
        c.meterFreqHz = t.meterFreqHz;
        c.halfWave = t.halfWave;
        chunks_.push_back(c);
        append(coreL, t.coreL, t.n);
        append(coreR, t.coreR, t.n);
        append(charL, t.charL, t.n);
        append(charR, t.charR, t.n);
        append(anchorL, t.anchorL, t.n);
        append(anchorR, t.anchorR, t.n);
        append(gAmp, t.gAmp, t.n);
        append(gD, t.gD, t.n);
        append(fRes, t.fRes, t.n);
        append(fCore, t.fCore, t.n);
        append(fAnchor, t.fAnchor, t.n);
        append(feed, t.feed, t.n);
    }

    [[nodiscard]] const std::vector<ChunkScalars>& chunks() const noexcept { return chunks_; }
    [[nodiscard]] const std::vector<ProfundumCallRecord>& calls() const noexcept { return calls_; }

    std::vector<float> coreL, coreR, charL, charR, anchorL, anchorR, gAmp, gD, fRes, fCore, fAnchor, feed;

private:
    [[nodiscard]] std::array<std::vector<float>*, 12> perSample() noexcept {
        return {&coreL, &coreR, &charL, &charR, &anchorL, &anchorR, &gAmp, &gD, &fRes, &fCore, &fAnchor, &feed};
    }
    static void append(std::vector<float>& dst, std::span<const float> src, std::size_t n) {
        const std::size_t m = std::min(n, src.size());
        dst.insert(dst.end(), src.begin(), src.begin() + static_cast<std::ptrdiff_t>(m));
        for (std::size_t i = m; i < n; ++i)   // a short span is a tap defect: pad so indices stay aligned
            dst.push_back(0.0f);
    }

    std::vector<ChunkScalars> chunks_;
    std::vector<ProfundumCallRecord> calls_;
};

/// One voice, its test-side field, its observer and the recorded sum.
struct VeRig {
    std::unique_ptr<ProfundumVoice> voice = std::make_unique<ProfundumVoice>();
    std::unique_ptr<BassStereoField> field = std::make_unique<BassStereoField>();
    VeObserver obs;
    ProfundumEngineSeams seams{};
    double fs = kVeFs;
    std::vector<float> sumL, sumR;
};

void vePrepare(VeRig& rig, const MorphState& s, double fs, std::size_t reserveSamples) {
    rig.fs = fs;
    rig.voice->prepare(fs, s);
    rig.field->setControls(s.field);
    rig.field->prepare(fs, kVeInterval);
    const std::size_t chunks = reserveSamples / kVeInterval + 64;
    rig.obs.reserve(reserveSamples + kVeInterval, chunks, chunks * 32);
    rig.sumL.reserve(reserveSamples + kVeInterval);
    rig.sumR.reserve(reserveSamples + kVeInterval);
    rig.voice->setObserverSlot(&rig.obs, 0, &rig.seams);
}

/// One control interval: field advanceControl, beginInterval with the hand-built IntervalInputs, then render in
/// chunks of `chunk` samples (sum buffers cleared to -0.0f per chunk; the contribution is appended to rig.sum).
void veInterval(VeRig& rig, const MorphState& s, float fResHz, std::size_t chunk = kVeInterval) {
    rig.field->setControls(s.field);
    rig.field->advanceControl();
    SpectralShapeRecipe::Controls cc = s.recipe;   // copy-then-assign (no Mass biases in the direct cases)
    cc.depth = s.recipe.depth;
    cc.body = s.recipe.body;
    const ProfundumVoice::IntervalInputs in{.state = &s,
                                            .coreControls = cc,
                                            .foldAmount = s.foldAmount,
                                            .anchorLevel = s.anchor.level,
                                            .field = rig.field.get(),
                                            .forceLinearCore = false};
    std::array<float, kVeInterval> fRes{};
    fRes.fill(fResHz);
    rig.voice->beginInterval(in, fRes.data(), kVeInterval);
    std::array<float, kVeInterval> l{};
    std::array<float, kVeInterval> r{};
    for (std::size_t k = 0; k < kVeInterval;) {
        const std::size_t m = std::min(chunk, kVeInterval - k);
        std::fill_n(l.data(), m, -0.0f);
        std::fill_n(r.data(), m, -0.0f);
        rig.voice->render(fRes.data() + k, l.data(), r.data(), m);
        rig.sumL.insert(rig.sumL.end(), l.begin(), l.begin() + static_cast<std::ptrdiff_t>(m));
        rig.sumR.insert(rig.sumR.end(), r.begin(), r.begin() + static_cast<std::ptrdiff_t>(m));
        k += m;
    }
}

[[nodiscard]] std::size_t veIntervalsFor(double fs, double seconds) {
    return static_cast<std::size_t>(seconds * fs) / kVeInterval;
}

/// The 18 per-interval steps of plan S5.4 in ProfundumCall order (S8: the order is normative).
constexpr std::array<ProfundumCall, 18> kVeIntervalSteps{
    ProfundumCall::VoicePan,      ProfundumCall::VoiceWide,     ProfundumCall::CoreControls,
    ProfundumCall::CoreExcitation, ProfundumCall::FoldAmount,   ProfundumCall::FoldF0,
    ProfundumCall::Shape,         ProfundumCall::CharAlgorithm, ProfundumCall::CharAmount,
    ProfundumCall::CharTone,      ProfundumCall::HalfWave,      ProfundumCall::FollowRatio,
    ProfundumCall::AnchorControls, ProfundumCall::LockBypass,   ProfundumCall::A1,
    ProfundumCall::DropSetters,   ProfundumCall::AmpEnv,        ProfundumCall::AmpCurve};

}  // namespace

// FR-010 (Phase 3 D-6): the three phase-policy enums map by static_cast; the header carries the same static_asserts.
TEST_CASE("ProfundumVoice_PhasePolicyMapping", "[systems][profundum][voice-engine][fr-010]") {
    using P = NoteMemory::PhasePolicy;
    using C = ProfundumCore::RetriggerPhase;
    using A = AnchorOscillator::RetriggerPhase;
    STATIC_REQUIRE(std::is_same_v<std::underlying_type_t<P>, std::underlying_type_t<C>>);
    STATIC_REQUIRE(std::is_same_v<std::underlying_type_t<P>, std::underlying_type_t<A>>);
    STATIC_REQUIRE(static_cast<int>(P::Reset) == 0);
    STATIC_REQUIRE(static_cast<int>(C::Reset) == 0);
    STATIC_REQUIRE(static_cast<int>(A::Reset) == 0);
    STATIC_REQUIRE(static_cast<int>(P::FreeRunning) == 1);
    STATIC_REQUIRE(static_cast<int>(C::FreeRunning) == 1);
    STATIC_REQUIRE(static_cast<int>(A::FreeRunning) == 1);
}

// SC-019(b) / FR-015 (plan S1 C-10, C-4): the voice's contribution is (c*gAmp)*gD + (a*gAmp)*gD in that float order,
// bitwise, over a render that contains a Reset retrigger dip (gD != 1).
TEST_CASE("ProfundumVoice_OutputIdentity", "[systems][profundum][voice-engine][fr-015][sc-019]") {
    enableFTZDAZ();
    const MorphState s = veStateAs();   // A_s: Anchor level 1 (showcase A), sustain 1 (plan S1 C-8)
    REQUIRE(s.anchor.level == 1.0f);
    const std::size_t intervals = veIntervalsFor(kVeFs, 1.0);
    const std::size_t tailAt = veIntervalsFor(kVeFs, 0.5);
    VeRig rig;
    vePrepare(rig, s, kVeFs, intervals * kVeInterval);
    rig.voice->setNoteMemory(NoteMemory::Mode::AlwaysRetrigger, NoteMemory::Retrigger::Reset);
    const float f = Krate::DSP::midiNoteToFrequency(kVeNoteE1);

    for (std::size_t j = 0; j < intervals; ++j) {
        if (j == 0)
            rig.voice->noteEvent(NoteMemory::Transition::FromSilence, f, false);
        if (j == tailAt) {
            REQUIRE(rig.voice->audible());
            rig.voice->noteEvent(NoteMemory::Transition::Tail, f, false);   // audible + Reset: a declick dip
        }
        veInterval(rig, s, f, j % 2 == 0 ? kVeInterval : std::size_t{7});   // whole and split chunks
    }

    const VeObserver& o = rig.obs;
    REQUIRE(o.charL.size() == rig.sumL.size());
    REQUIRE(rig.sumL.size() == intervals * kVeInterval);
    std::size_t mismatches = 0;
    std::size_t dipSamples = 0;
    for (std::size_t i = 0; i < rig.sumL.size(); ++i) {
        const float ga = o.gAmp[i];
        const float gd = o.gD[i];
        const float pL = o.charL[i] * ga;
        const float qL = o.anchorL[i] * ga;
        const float expL = pL * gd + qL * gd;
        const float pR = o.charR[i] * ga;
        const float qR = o.anchorR[i] * ga;
        const float expR = pR * gd + qR * gd;
        if (!veSameBits(rig.sumL[i], expL) || !veSameBits(rig.sumR[i], expR))
            ++mismatches;
        if (!veSameBits(gd, 1.0f))
            ++dipSamples;
    }
    std::cout << std::setprecision(6) << "ProfundumVoice_OutputIdentity: samples " << rig.sumL.size()
              << ", mismatches " << mismatches << ", dip samples " << dipSamples << "\n";
    CHECK(mismatches == 0);
    CHECK(dipSamples > 0);
}

// FR-003 / FR-004 (plan S1 C-10, S9.5): at g = 0 the feed is skipped, so the core and Character taps equal a voice
// with anchorDetached bitwise; g stepping 0 -> 1 ramps feed[k] = from + (to - from) * float(k + 1) / 32 to 1.0f.
TEST_CASE("ProfundumVoice_FeedSkipAndRamp", "[systems][profundum][voice-engine][fr-003][c-10]") {
    enableFTZDAZ();
    MorphState s = veStateAs();
    s.anchorFoldFeed = 0.0f;
    constexpr std::size_t kZeroIntervals = 50;
    VeRig attached;
    VeRig detached;
    detached.seams.anchorDetached = true;
    vePrepare(attached, s, kVeFs, (kZeroIntervals + 4) * kVeInterval);
    vePrepare(detached, s, kVeFs, (kZeroIntervals + 4) * kVeInterval);
    const float f = Krate::DSP::midiNoteToFrequency(kVeNoteE1);
    for (std::size_t j = 0; j < kZeroIntervals; ++j) {
        if (j == 0) {
            attached.voice->noteEvent(NoteMemory::Transition::FromSilence, f, false);
            detached.voice->noteEvent(NoteMemory::Transition::FromSilence, f, false);
        }
        veInterval(attached, s, f);
        veInterval(detached, s, f);
    }
    const std::size_t n0 = kZeroIntervals * kVeInterval;
    REQUIRE(attached.obs.feed.size() == n0);
    REQUIRE(detached.obs.coreL.size() == n0);
    std::size_t feedNonZero = 0;
    std::size_t tapMismatch = 0;
    for (std::size_t i = 0; i < n0; ++i) {
        if (std::bit_cast<std::uint32_t>(attached.obs.feed[i]) != 0u)
            ++feedNonZero;
        if (!veSameBits(attached.obs.coreL[i], detached.obs.coreL[i])
            || !veSameBits(attached.obs.coreR[i], detached.obs.coreR[i])
            || !veSameBits(attached.obs.charL[i], detached.obs.charL[i])
            || !veSameBits(attached.obs.charR[i], detached.obs.charR[i]))
            ++tapMismatch;
    }
    CHECK(feedNonZero == 0);
    CHECK(tapMismatch == 0);

    // g steps 0 -> 1 between two intervals (S9.5: the ramp lands on g_to at k = 31).
    s.anchorFoldFeed = 1.0f;
    veInterval(attached, s, f);
    const VeObserver::ChunkScalars& c = attached.obs.chunks().back();
    REQUIRE(c.n == kVeInterval);
    CHECK(veSameBits(c.feedFrom, 0.0f));
    CHECK(veSameBits(c.feedTo, 1.0f));
    std::size_t rampMismatch = 0;
    for (std::size_t k = 0; k < kVeInterval; ++k) {
        const float expected = c.feedFrom + (c.feedTo - c.feedFrom) * static_cast<float>(k + 1) / 32.0f;
        if (!veSameBits(attached.obs.feed[n0 + k], expected))
            ++rampMismatch;
    }
    CHECK(rampMismatch == 0);
    CHECK(veSameBits(attached.obs.feed[n0 + kVeInterval - 1], 1.0f));
}

// FR-005 (plan S1 C-6, S9.7): a1 from the cached shape / mask stages equals unbiasedH1Amplitude bitwise while the
// recipe steps and the Anchor's pitch input changes.
TEST_CASE("ProfundumVoice_A1MatchesUnbiasedH1", "[systems][profundum][voice-engine][fr-005][c-6]") {
    enableFTZDAZ();
    MorphState s = veStateAs();
    constexpr std::size_t kIntervals = 20;
    VeRig rig;
    vePrepare(rig, s, kVeFs, (kIntervals + 2) * kVeInterval);
    const std::array<int, 3> notes{kVeNoteE1, kVeNoteA1, kVeNoteC3};
    std::size_t mismatches = 0;
    for (std::size_t j = 0; j < kIntervals; ++j) {
        s.recipe.depth = 0.1f + 0.8f * static_cast<float>(j) / static_cast<float>(kIntervals - 1);
        const float f = Krate::DSP::midiNoteToFrequency(notes[j % notes.size()]);
        if (j == 0)
            rig.voice->noteEvent(NoteMemory::Transition::FromSilence, f, false);
        const std::size_t first = rig.obs.chunks().size();
        veInterval(rig, s, f);
        REQUIRE(rig.obs.chunks().size() > first);
        const VeObserver::ChunkScalars& c = rig.obs.chunks()[first];
        const float fAnchor0 = rig.obs.fAnchor[c.offset];
        CHECK(veSameBits(fAnchor0, f));   // LOCK: the Anchor reads f_res (FR-006)
        const float ref = Krate::DSP::MassDynamicsTest::unbiasedH1Amplitude(s.recipe, static_cast<double>(fAnchor0),
                                                                           kVeFs);
        if (!veSameBits(c.a1, ref)) {
            ++mismatches;
            std::cout << std::setprecision(6) << "a1 mismatch at interval " << j << ": tap " << c.a1 << ", ref "
                      << ref << "\n";
        }
    }
    CHECK(mismatches == 0);
}

// FR-013 / FR-054 (plan S1 C-20): every step is change-only; the first interval after prepare / reset issues all.
TEST_CASE("ProfundumVoice_ChangeOnlySetters", "[systems][profundum][voice-engine][fr-013][c-20]") {
    enableFTZDAZ();
    const MorphState s = veStateAs();
    const std::size_t warm = veIntervalsFor(kVeFs, 1.0);
    VeRig rig;
    vePrepare(rig, s, kVeFs, (warm + 210) * kVeInterval);
    const float f = Krate::DSP::midiNoteToFrequency(kVeNoteE1);

    const auto firstIntervalIssuesAll = [&](const char* label) {
        rig.obs.clear();
        rig.voice->noteEvent(NoteMemory::Transition::FromSilence, f, false);
        veInterval(rig, s, f);
        for (const ProfundumCall step : kVeIntervalSteps) {
            std::size_t records = 0;
            std::size_t issued = 0;
            for (const ProfundumCallRecord& r : rig.obs.calls()) {
                if (r.call != step)
                    continue;
                ++records;
                if (r.issued)
                    ++issued;
            }
            INFO(label << ": step " << static_cast<int>(step));
            CHECK(records == 1);
            CHECK(issued == 1);
        }
        // The first note after prepare / reset takes the Reset branch: syncPhase and both note-ons are issued.
        std::size_t sync = 0;
        for (const ProfundumCallRecord& r : rig.obs.calls())
            sync += (r.call == ProfundumCall::SyncPhase && r.issued) ? 1u : 0u;
        CHECK(sync == 1);
    };

    firstIntervalIssuesAll("after prepare");
    for (std::size_t j = 1; j < warm; ++j)
        veInterval(rig, s, f);
    rig.obs.clear();
    for (std::size_t j = 0; j < 200; ++j)
        veInterval(rig, s, f);
    std::size_t issuedUnchanged = 0;
    std::size_t perIntervalRecords = 0;
    for (const ProfundumCallRecord& r : rig.obs.calls()) {
        if (r.issued) {
            ++issuedUnchanged;
            std::cout << "issued while unchanged: call " << static_cast<int>(r.call) << " at sample " << r.sample
                      << "\n";
        }
        if (std::find(kVeIntervalSteps.begin(), kVeIntervalSteps.end(), r.call) != kVeIntervalSteps.end())
            ++perIntervalRecords;
    }
    CHECK(issuedUnchanged == 0);
    CHECK(perIntervalRecords == 200 * kVeIntervalSteps.size());   // every step still emits its record

    rig.voice->reset(s);
    firstIntervalIssuesAll("after reset");
}

// FR-008 (plan S1 C-5, S9.6): note 127 + a 24 st bend puts f_res above fs/2; the meter integrates the clamped f_core,
// so theta stays in [0, 1), the meter frequency is the core's clamp of f_core (never above capHz), and the output
// stays finite. The clamp is read from the f_core tap at the chunk's last 16-sample pitch update (S9.6), not assumed to
// be capHz: in LOCK the note's DropPhaseLock bump (d_c = gamma * b, drop_phase_lock.h:595) dips f_core a few cents
// below the cap for ~8 chunks after the note, so f_core >= capHz does not hold at every chunk.
TEST_CASE("ProfundumVoice_MeterBounded", "[systems][profundum][voice-engine][fr-008][c-5]") {
    enableFTZDAZ();
    const MorphState s = veStateAs();
    const float fRes = Krate::DSP::midiNoteToFrequency(127) * Krate::DSP::semitonesToRatio(24.0f);
    for (const double fs : {44100.0, 48000.0}) {
        INFO("fs " << fs);
        REQUIRE(static_cast<double>(fRes) > 0.5 * fs);
        const std::size_t intervals = veIntervalsFor(fs, 2.0);
        VeRig rig;
        vePrepare(rig, s, fs, intervals * kVeInterval);
        rig.voice->noteEvent(NoteMemory::Transition::FromSilence, fRes, false);
        for (std::size_t j = 0; j < intervals; ++j)
            veInterval(rig, s, fRes);
        const float capHz = SpectralShapeRecipe::capFrequency(fs);
        std::size_t thetaOut = 0;
        std::size_t freqOff = 0;
        std::size_t aboveCap = 0;
        std::size_t atCap = 0;
        constexpr std::uint64_t kU = ProfundumCore::kPitchUpdateInterval;   // the meter's setFrequency grid
        for (const VeObserver::ChunkScalars& c : rig.obs.chunks()) {
            if (c.meterTheta < 0.0f || c.meterTheta >= 1.0f)
                ++thetaOut;
            std::size_t last = 0;   // the note-on chunk's offset 0, else the last grid sample in the chunk
            for (std::size_t i = 0; i < c.n; ++i) {
                if ((c.sample + i) % kU == 0u)
                    last = i;
            }
            const float expected = std::clamp(rig.obs.fCore[c.offset + last], ProfundumCore::kMinF0Hz, capHz);
            if (!veSameBits(c.meterFreqHz, expected))
                ++freqOff;
            if (c.meterFreqHz > capHz)
                ++aboveCap;
            if (veSameBits(c.meterFreqHz, capHz))
                ++atCap;
        }
        std::size_t nonFinite = 0;
        for (std::size_t i = 0; i < rig.sumL.size(); ++i) {
            if (!Krate::DSP::detail::isFinite(rig.sumL[i]) || !Krate::DSP::detail::isFinite(rig.sumR[i]))
                ++nonFinite;
        }
        std::cout << std::setprecision(6) << "ProfundumVoice_MeterBounded fs " << fs << ": chunks "
                  << rig.obs.chunks().size() << ", theta out of range " << thetaOut << ", freq != clamp(f_core) " << freqOff
                  << ", above cap " << aboveCap << ", at cap " << atCap << ", non-finite " << nonFinite << "\n";
        CHECK_FALSE(rig.obs.chunks().empty());
        CHECK(thetaOut == 0);
        CHECK(freqOff == 0);
        CHECK(aboveCap == 0);
        CHECK(atCap + 16 >= rig.obs.chunks().size());   // the clamp binds everywhere but the lock bump (8 chunks seen)
        CHECK(nonFinite == 0);
        CHECK(rig.voice->stateFinite());
    }
}

// FR-015: a voice is idle (not active, not audible) once its released envelope ends; a pending dip keeps it active.
TEST_CASE("ProfundumVoice_IdleNotActive", "[systems][profundum][voice-engine][fr-015]") {
    enableFTZDAZ();
    const MorphState s = veStateAs();   // release 50 ms
    VeRig rig;
    vePrepare(rig, s, kVeFs, 3 * static_cast<std::size_t>(kVeFs));
    rig.voice->setNoteMemory(NoteMemory::Mode::AlwaysRetrigger, NoteMemory::Retrigger::Reset);
    const float f = Krate::DSP::midiNoteToFrequency(kVeNoteE1);

    CHECK_FALSE(rig.voice->active());
    CHECK_FALSE(rig.voice->audible());
    rig.voice->noteEvent(NoteMemory::Transition::FromSilence, f, false);
    CHECK(rig.voice->active());
    for (std::size_t j = 0; j < veIntervalsFor(kVeFs, 0.2); ++j)
        veInterval(rig, s, f);
    CHECK(rig.voice->active());
    CHECK(rig.voice->audible());
    CHECK(rig.voice->held());

    rig.voice->release();
    CHECK_FALSE(rig.voice->held());
    for (std::size_t j = 0; j < veIntervalsFor(kVeFs, 1.0); ++j)
        veInterval(rig, s, f);
    CHECK_FALSE(rig.voice->audible());
    CHECK_FALSE(rig.voice->active());

    // A dip pending: an audible voice retriggered under Reset (C-4).
    rig.voice->noteEvent(NoteMemory::Transition::FromSilence, f, false);
    for (std::size_t j = 0; j < veIntervalsFor(kVeFs, 0.1); ++j)
        veInterval(rig, s, f);
    REQUIRE(rig.voice->audible());
    rig.voice->noteEvent(NoteMemory::Transition::Tail, f, false);
    CHECK(rig.voice->active());
    veInterval(rig, s, f);   // L_d = 72 samples at 48 kHz: the switch is still ahead after one interval
    CHECK(rig.voice->active());
    const std::size_t n = rig.obs.gD.size();
    REQUIRE(n >= kVeInterval);
    CHECK(rig.obs.gD[n - 1] < 1.0f);   // the dip is running
}

// =============================================================================================================
// T010: the voice criteria through the engine (SC-019, SC-020, SC-033, SC-034; FR-006, FR-008; plan S10.3, S10.6)
// =============================================================================================================

namespace {

namespace PT = Krate::DSP::ProfundumTest;
using Krate::DSP::BassMorphEngine;
using Krate::DSP::DropEnvelope;
using Krate::DSP::DropPhaseLock;
using Krate::DSP::GlideMode;
using Krate::DSP::MonoHandler;
using Krate::DSP::MonoMode;
using Krate::DSP::NoteProcessor;
using Krate::DSP::PortaMode;
using Krate::DSP::ProfundumEngine;
using Krate::DSP::RetriggerDeclick;
using VeKind = PT::VoiceEngineEventKind;
using VeMode = ProfundumEngine::VoiceMode;

constexpr std::uint64_t kVeGrid = ProfundumEngine::kControlInterval;
constexpr double kVePi = 3.14159265358979323846;

/// A grid-aligned sample (events land on grid points, so "latched at the sample" and "applied at the grid point"
/// coincide, C-9).
[[nodiscard]] std::uint64_t veAt(double seconds, double fs) {
    const auto n = static_cast<std::uint64_t>(std::llround(seconds * fs));
    return (n / kVeGrid) * kVeGrid;
}

[[nodiscard]] PT::VoiceEngineEvent veOn(std::uint64_t at, std::uint8_t note) {
    return PT::VoiceEngineEvent{.sample = at, .kind = VeKind::NoteOn, .note = note};
}
[[nodiscard]] PT::VoiceEngineEvent veOff(std::uint64_t at, std::uint8_t note) {
    return PT::VoiceEngineEvent{.sample = at, .kind = VeKind::NoteOff, .note = note};
}
[[nodiscard]] PT::VoiceEngineEvent veEv(std::uint64_t at, VeKind kind, float value, int index = 0) {
    return PT::VoiceEngineEvent{.sample = at, .kind = kind, .value = value, .index = index};
}

/// One script: endpoints A = B = s at position 0 (no morph unless an event moves it).
[[nodiscard]] PT::VoiceEngineScript veScript(const MorphState& s, double seconds, VeMode mode, std::size_t tapped,
                                             double fs = kVeFs) {
    PT::VoiceEngineScript sc;
    sc.fs = fs;
    sc.totalSamples = static_cast<std::uint64_t>(std::llround(seconds * fs));
    sc.a = s;
    sc.b = s;
    sc.mode = mode;
    sc.tappedVoices = tapped;
    return sc;
}

/// PT::voiceEngineRender (profundum_voice_engine_test_helpers.h:369-427) with one addition: setPitchBendRange before
/// prepare (VoiceEngineEventKind has no bend-range event). Same allocation, observer, issue and split rules.
[[nodiscard]] PT::VoiceEngineTaps veRenderWithBendRange(const PT::VoiceEngineScript& s, float bendRangeSt) {
    REQUIRE(s.totalSamples > 0);
    REQUIRE(s.hostBlock >= 1);
    std::vector<PT::VoiceEngineEvent> events = s.events;
    std::stable_sort(events.begin(), events.end(),
                     [](const PT::VoiceEngineEvent& x, const PT::VoiceEngineEvent& y) { return x.sample < y.sample; });
    PT::VoiceEngineTaps taps;
    PT::voiceEngineAllocateTaps(taps, s);
    PT::VoiceEngineObserver observer(taps);
    auto e = std::make_unique<ProfundumEngine>();
    e->setSeed(s.seed);
    REQUIRE(e->setEndpoint(BassMorphEngine::Endpoint::A, s.a));
    REQUIRE(e->setEndpoint(BassMorphEngine::Endpoint::B, s.b));
    e->setMorphPosition(s.position);
    e->setMacros(s.macros);
    e->setVoiceMode(s.mode);
    e->setPolyphony(s.polyphony);
    e->setPitchBendRange(bendRangeSt);
    REQUIRE(veSameBits(e->pitchBendRange(), bendRangeSt));
    if (s.attachObserver)
        e->setObserver(&observer);
    e->prepare(s.fs, s.config);
    const auto total = static_cast<std::size_t>(s.totalSamples);
    std::size_t pos = 0;
    std::size_t ei = 0;
    while (pos < total) {
        const std::size_t blockEnd = std::min(pos + s.hostBlock, total);
        while (pos < blockEnd) {
            while (ei < events.size() && events[ei].sample <= pos)
                PT::voiceEngineIssue(*e, events[ei++], s);
            std::size_t next = blockEnd;
            if (ei < events.size() && events[ei].sample < next)
                next = static_cast<std::size_t>(events[ei].sample);
            e->processBlock(taps.outL.data() + pos, taps.outR.data() + pos, next - pos);
            pos = next;
        }
    }
    taps.droppedEvents = e->droppedEvents();
    e->setObserver(nullptr);
    return taps;
}

/// 1 where the voice rendered sample i (from its chunk records), else 0.
[[nodiscard]] std::vector<std::uint8_t> veRenderedMask(const PT::VoiceEngineVoiceTapSet& v, std::size_t total) {
    std::vector<std::uint8_t> m(total, 0);
    for (const PT::VoiceEngineChunkRecord& c : v.chunks) {
        for (std::size_t i = 0; i < c.n; ++i) {
            const auto k = static_cast<std::size_t>(c.sample) + i;
            if (k < total)
                m[k] = 1;
        }
    }
    return m;
}

[[nodiscard]] double veCents(double a, double b) { return 1200.0 * std::log2(a / b); }

// ---- SC-019 ---------------------------------------------------------------------------------------------------

struct VeJoin {
    std::size_t mismatches = 0;
    std::size_t singleVoiceSamples = 0;
    std::vector<std::uint64_t> dipGrids;   // grid points of the intervals with g_d != 1 on some voice
};

/// SC-019(b): the Sum tap equals, per sample and channel, the voices' FR-015 contributions added in voice-index order
/// onto -0 (the engine's fill, profundum_engine.h renderChunk step 1), exactly as the voice adds them
/// (profundum_voice.h renderSub steps 9-10: out += (c * ga) * gd + (a * ga) * gd). With one voice rendering, the Sum is
/// that voice's contribution bit for bit.
[[nodiscard]] VeJoin veJoinIdentity(const PT::VoiceEngineTaps& t, std::size_t voices) {
    VeJoin r;
    std::vector<std::vector<std::uint8_t>> masks;
    for (std::size_t v = 0; v < voices; ++v)
        masks.push_back(veRenderedMask(t.voices[v], t.total));
    for (std::size_t i = 0; i < t.total; ++i) {
        float eL = -0.0f;
        float eR = -0.0f;
        std::size_t count = 0;
        bool dip = false;
        for (std::size_t v = 0; v < voices; ++v) {
            if (masks[v][i] == 0)
                continue;
            const PT::VoiceEngineVoiceTapSet& p = t.voices[v];
            const float ga = p.gAmp[i];
            const float gd = p.gD[i];
            const float pL = p.charL[i] * ga;
            const float qL = p.anchorL[i] * ga;
            const float pR = p.charR[i] * ga;
            const float qR = p.anchorR[i] * ga;
            eL += pL * gd + qL * gd;
            eR += pR * gd + qR * gd;
            ++count;
            if (!veSameBits(gd, 1.0f))
                dip = true;
        }
        if (!veSameBits(t.sumL[i], eL) || !veSameBits(t.sumR[i], eR))
            ++r.mismatches;
        if (count == 1)
            ++r.singleVoiceSamples;
        if (dip) {
            const std::uint64_t g = (static_cast<std::uint64_t>(i) / kVeGrid) * kVeGrid;
            if (r.dipGrids.empty() || r.dipGrids.back() != g)
                r.dipGrids.push_back(g);
        }
    }
    return r;
}

[[nodiscard]] std::string veEvidenceDir() {
    return std::string(KRATE_DSP_TESTS_DIR) + "/../../specs/profundum-phase9-voice-engine/evidence/";
}

[[nodiscard]] MorphState veWithFeed(float g) {
    MorphState s = veStateAs();   // A_s: Anchor level 1 (showcase A), sustain 1 (C-8)
    s.anchorFoldFeed = g;
    return s;
}

/// MONO: E1, then A1 overlapping at 0.3 s (AlwaysRetrigger / Reset, the engine default: an audible Reset restart, so
/// a declick dip), both off at 0.9 s.
[[nodiscard]] PT::VoiceEngineScript veMonoRetriggerScript(const MorphState& s) {
    PT::VoiceEngineScript sc = veScript(s, 1.0, VeMode::Mono, 1);
    sc.events = {veOn(0, PT::kVoiceEngineNoteE1), veOn(veAt(0.3, kVeFs), PT::kVoiceEngineNoteA1),
                 veOff(veAt(0.9, kVeFs), PT::kVoiceEngineNoteA1), veOff(veAt(0.9, kVeFs), PT::kVoiceEngineNoteE1)};
    return sc;
}

/// POLY 4: E1 / A1 / E2 / C3, then G1 at 0.3 s steals the oldest voice (Oldest / Hard: Transition::VoiceReuse, a
/// dip); A1 / E2 / C3 off at 0.5 s, so the stolen voice sounds alone from their release end to 1 s.
[[nodiscard]] PT::VoiceEngineScript vePolyStealScript(const MorphState& s) {
    PT::VoiceEngineScript sc = veScript(s, 1.0, VeMode::Poly, ProfundumEngine::kDefaultPolyphony);
    sc.polyphony = ProfundumEngine::kDefaultPolyphony;
    const std::uint64_t off = veAt(0.5, kVeFs);
    sc.events = {veOn(0, PT::kVoiceEngineNoteE1),
                 veOn(veAt(0.02, kVeFs), PT::kVoiceEngineNoteA1),
                 veOn(veAt(0.04, kVeFs), PT::kVoiceEngineNoteE2),
                 veOn(veAt(0.06, kVeFs), PT::kVoiceEngineNoteC3),
                 veOn(veAt(0.3, kVeFs), PT::kVoiceEngineNoteG1),
                 veOff(off, PT::kVoiceEngineNoteA1),
                 veOff(off, PT::kVoiceEngineNoteE2),
                 veOff(off, PT::kVoiceEngineNoteC3)};
    return sc;
}

void veWriteJoinLine(std::ofstream& ev, const char* label, float g, const PT::VoiceEngineTaps& t, const VeJoin& j) {
    std::cout << "VoiceEngine_AnchorJoin (b) g " << g << " " << label << ": samples " << t.total << ", mismatches "
              << j.mismatches << ", single-voice samples " << j.singleVoiceSamples << ", g_d != 1 intervals "
              << j.dipGrids.size() << "\n";
    ev << "(b) g " << g << " " << label << ": samples " << t.total << ", mismatches " << j.mismatches
       << ", single-voice samples " << j.singleVoiceSamples << ", g_d != 1 intervals " << j.dipGrids.size() << " at";
    for (const std::uint64_t gs : j.dipGrids)
        ev << " " << gs;
    ev << "\n";
}

// ---- SC-020 ---------------------------------------------------------------------------------------------------

struct VeRefNote {
    std::uint64_t sample = 0;
    std::uint8_t note = 0;
    NoteMemory::Transition transition = NoteMemory::Transition::FromSilence;
    bool audible = false;
};

struct VeRefTrack {
    std::uint64_t start = std::numeric_limits<std::uint64_t>::max();
    std::vector<float> fRes, fCore;
};

/// SC-020's independent reference (plan C-11): a test-side MonoHandler (MONO only) + NoteProcessor + DropEnvelope +
/// DropPhaseLock per track, configured as the engine and the voice configure theirs (profundum_engine.h prepare step 4
/// and reset's applySettings(true); profundum_voice.h prepare / reset) and driven at the same grid points: per grid
/// point the changed setters, then the notes, then 32 x {b = processPitchBend(); ratio = semitonesToRatio(b * range);
/// fMono = processPortamento() * ratio}; per sample f_res = fMono (MONO) or midiNoteToFrequency(note) * ratio (POLY),
/// and from the track's trigger sample d = drop.process(), d_c = lock.process(d), f_core = f_res * std::exp2(d_c /
/// 12.0f). A trigger lands on the note's grid point or, for an audible Reset restart (NoteMemory's declick), on the
/// RetriggerDeclick switch sample (profundum_voice.h applySwitch). The lock is bypassed in FOLLOW only. MONO: one
/// track; POLY: one track per note. Note Memory AlwaysRetrigger / Reset (the engine default).
[[nodiscard]] std::vector<VeRefTrack> veDropReference(double fs, std::size_t total, bool mono, const MorphState& s,
                                                      float portamentoMs, float bendRangeSt,
                                                      const std::vector<VeRefNote>& notes,
                                                      const std::vector<PT::VoiceEngineEvent>& events) {
    const std::size_t tracks = mono ? std::size_t{1} : notes.size();
    std::vector<VeRefTrack> out(tracks);
    for (VeRefTrack& t : out) {
        t.fRes.assign(total, 0.0f);
        t.fCore.assign(total, 0.0f);
    }
    const bool follow = s.anchor.mode == AnchorOscillator::Mode::Follow;
    const double fsCore = std::clamp(fs, ProfundumCore::kMinSampleRate, ProfundumCore::kMaxSampleRate);
    const float capHz = SpectralShapeRecipe::capFrequency(fsCore);   // the voice's capHz_ (profundum_voice.h prepare)

    std::vector<DropEnvelope> drops(tracks);
    std::vector<DropPhaseLock> locks(tracks);
    for (std::size_t k = 0; k < tracks; ++k) {
        drops[k].prepare(fs);
        locks[k].prepare(fs);
        drops[k].setAmount(s.dropAmountSt);
        drops[k].setTime(s.dropTimeMs);
        drops[k].setShape(s.dropShape);
        drops[k].setRebound(s.dropRebound);
        locks[k].setBypass(follow);
        drops[k].reset();
        locks[k].reset();
    }
    std::vector<std::uint64_t> trig(tracks, std::numeric_limits<std::uint64_t>::max());
    std::vector<std::uint8_t> trigSnap(tracks, 0);
    std::vector<DropPhaseLock::TriggerKind> kinds(tracks, DropPhaseLock::TriggerKind::SnapReset);
    std::vector<std::uint8_t> started(tracks, 0);
    std::vector<float> noteHz(tracks, 0.0f);

    MonoHandler monoH;
    NoteProcessor bend;
    monoH.prepare(fs);
    bend.prepare(fs);
    monoH.reset();
    bend.reset();
    monoH.setMode(MonoMode::LastNote);
    monoH.setLegato(false);
    monoH.setPortamentoTime(0.0f);
    monoH.setPortamentoMode(PortaMode::Always);
    monoH.setGlideMode(GlideMode::ConstantTime);
    bend.setPitchBend(0.0f);
    bend.setPitchBendRange(bendRangeSt);

    RetriggerDeclick dk;
    dk.prepare(fs, ProfundumVoice::kDeclickMs);   // R-B
    dk.arm();
    const auto untilSwitch = static_cast<std::uint64_t>(dk.samplesUntilSwitch());

    std::vector<PT::VoiceEngineEvent> bends;
    for (const PT::VoiceEngineEvent& e : events) {
        if (e.kind == VeKind::PitchBend)
            bends.push_back(e);
    }
    std::stable_sort(bends.begin(), bends.end(),
                     [](const PT::VoiceEngineEvent& x, const PT::VoiceEngineEvent& y) { return x.sample < y.sample; });

    float pendingBend = 0.0f;
    float appliedBend = 0.0f;
    std::size_t bi = 0;
    std::size_t ni = 0;
    std::array<float, kVeGrid> ratio{};
    std::array<float, kVeGrid> monoF{};
    for (std::uint64_t g = 0; g < total; g += kVeGrid) {
        if (g == 0 && !veSameBits(portamentoMs, 0.0f))
            monoH.setPortamentoTime(portamentoMs);
        while (bi < bends.size() && bends[bi].sample <= g)
            pendingBend = std::clamp(bends[bi++].value, -1.0f, 1.0f);
        if (!veSameBits(pendingBend, appliedBend)) {
            bend.setPitchBend(pendingBend);
            appliedBend = pendingBend;
        }
        while (ni < notes.size() && notes[ni].sample <= g) {
            const VeRefNote& n = notes[ni];
            const std::size_t tr = mono ? std::size_t{0} : ni;
            if (mono)
                static_cast<void>(monoH.noteOn(n.note, 100));
            const NoteMemory::Actions a = NoteMemory::resolve(NoteMemory::Mode::AlwaysRetrigger,
                                                              NoteMemory::Retrigger::Reset, n.transition, n.audible);
            REQUIRE(a.dropTrigger);
            REQUIRE(a.phase == NoteMemory::PhasePolicy::Reset);   // the Reset branch: grid phase 0 at the trigger
            trig[tr] = g + (a.declick ? untilSwitch : std::uint64_t{0});
            trigSnap[tr] = a.dropSnap ? 1 : 0;
            kinds[tr] = a.dropSnap ? DropPhaseLock::TriggerKind::SnapReset : DropPhaseLock::TriggerKind::Bridged;
            noteHz[tr] = Krate::DSP::midiNoteToFrequency(static_cast<int>(n.note));
            ++ni;
        }
        for (std::size_t k = 0; k < kVeGrid; ++k) {
            const float b = bend.processPitchBend();
            ratio[k] = Krate::DSP::semitonesToRatio(b * bendRangeSt);
            monoF[k] = monoH.processPortamento() * ratio[k];
        }
        for (std::size_t k = 0; k < kVeGrid; ++k) {
            const auto n = static_cast<std::size_t>(g) + k;
            if (n >= total)
                break;
            for (std::size_t tr = 0; tr < tracks; ++tr) {
                const float fRes = mono ? monoF[k] : noteHz[tr] * ratio[k];
                if (static_cast<std::uint64_t>(n) == trig[tr]) {
                    drops[tr].trigger(trigSnap[tr] != 0);
                    locks[tr].trigger(drops[tr].plannedCurve(), fRes, 0, ProfundumCore::kMinF0Hz, capHz, kinds[tr]);
                    if (started[tr] == 0) {
                        started[tr] = 1;
                        out[tr].start = static_cast<std::uint64_t>(n);
                    }
                }
                if (started[tr] == 0)
                    continue;
                const float d = drops[tr].process();
                const float dc = locks[tr].process(d);
                out[tr].fRes[n] = fRes;
                out[tr].fCore[n] = fRes * std::exp2(dc / 12.0f);
            }
        }
    }
    return out;
}

struct VePitchGate {
    std::size_t compared = 0;
    std::size_t coreMismatch = 0;
    std::size_t anchorMismatch = 0;
    std::size_t resMismatch = 0;
    double maxCoreCents = 0.0;
    double maxAnchorCents = 0.0;
};

/// Per rendered sample from the track's first trigger: the core pitch tap vs fCore_ref, the Anchor pitch tap vs
/// fRes_ref (LOCK / PUNCH / GHOST) or fCore_ref (FOLLOW), the f_res tap vs fRes_ref; bitwise counts and max cents.
void veGatePitch(const PT::VoiceEngineVoiceTapSet& v, const VeRefTrack& ref, bool anchorReadsCore, std::size_t total,
                 VePitchGate& g) {
    const std::vector<std::uint8_t> mask = veRenderedMask(v, total);
    for (auto n = static_cast<std::size_t>(ref.start); n < total; ++n) {
        if (mask[n] == 0)
            continue;
        ++g.compared;
        const float wantAnchor = anchorReadsCore ? ref.fCore[n] : ref.fRes[n];
        if (!veSameBits(v.fCore[n], ref.fCore[n])) {
            ++g.coreMismatch;
            g.maxCoreCents = std::max(g.maxCoreCents, std::abs(veCents(v.fCore[n], ref.fCore[n])));
        }
        if (!veSameBits(v.fAnchor[n], wantAnchor)) {
            ++g.anchorMismatch;
            g.maxAnchorCents = std::max(g.maxAnchorCents, std::abs(veCents(v.fAnchor[n], wantAnchor)));
        }
        if (!veSameBits(v.fRes[n], ref.fRes[n]))
            ++g.resMismatch;
    }
}

/// The voice whose first chunk record starts at `sample`, or kMaxVoices.
[[nodiscard]] std::size_t veVoiceStartingAt(const PT::VoiceEngineTaps& t, std::uint64_t sample) {
    for (std::size_t v = 0; v < t.tappedVoices; ++v) {
        if (!t.voices[v].chunks.empty() && t.voices[v].chunks.front().sample == sample)
            return v;
    }
    return ProfundumEngine::kMaxVoices;
}

/// The SC-020 Drop state: A_s with a 24 st, 200 ms Drop and the given Anchor mode.
[[nodiscard]] MorphState veDropState(AnchorOscillator::Mode mode) {
    MorphState s = veStateAs();
    s.dropAmountSt = 24.0f;
    s.dropTimeMs = 200.0f;
    s.anchor.mode = mode;
    return s;
}

/// A bend ramp 0 -> +1 -> -1 over [from, to], one event per grid point.
void veAddBendRamp(std::vector<PT::VoiceEngineEvent>& ev, std::uint64_t from, std::uint64_t to) {
    const auto span = static_cast<double>(to - from);
    for (std::uint64_t g = from; g < to; g += kVeGrid) {
        const double x = static_cast<double>(g - from) / span;
        const double b = x < 0.5 ? 2.0 * x : 1.0 - 4.0 * (x - 0.5);
        ev.push_back(veEv(g, VeKind::PitchBend, static_cast<float>(b)));
    }
    ev.push_back(veEv(to, VeKind::PitchBend, -1.0f));
}

constexpr float kVeGlideMs = 200.0f;

/// SC-020 MONO script: E1, then E2 overlapping at 0.1 s (the 200 ms glide; an audible Reset restart, so the Drop
/// retriggers at the dip's switch sample), the bend ramp over 0.12 ... 0.32 s, during the E2 Drop.
[[nodiscard]] PT::VoiceEngineScript veDropMonoScript(const MorphState& s, std::vector<VeRefNote>& notes) {
    PT::VoiceEngineScript sc = veScript(s, 0.6, VeMode::Mono, 1);
    const std::uint64_t e2 = veAt(0.1, kVeFs);
    sc.events = {veEv(0, VeKind::PortamentoTimeMs, kVeGlideMs), veOn(0, PT::kVoiceEngineNoteE1),
                 veOn(e2, PT::kVoiceEngineNoteE2)};
    veAddBendRamp(sc.events, veAt(0.12, kVeFs), veAt(0.32, kVeFs));
    notes = {VeRefNote{.sample = 0,
                       .note = PT::kVoiceEngineNoteE1,
                       .transition = NoteMemory::Transition::FromSilence,
                       .audible = false},
             VeRefNote{.sample = e2,
                       .note = PT::kVoiceEngineNoteE2,
                       .transition = NoteMemory::Transition::Overlap,
                       .audible = true}};
    return sc;
}

/// SC-020 POLY script: E1 / A1 / E2 / C3 staggered by 30 ms (each FromSilence on its own voice, each with the Drop),
/// the same bend ramp.
[[nodiscard]] PT::VoiceEngineScript veDropPolyScript(const MorphState& s, std::vector<VeRefNote>& notes) {
    PT::VoiceEngineScript sc = veScript(s, 0.6, VeMode::Poly, ProfundumEngine::kDefaultPolyphony);
    sc.polyphony = ProfundumEngine::kDefaultPolyphony;
    const std::array<std::uint8_t, 4> keys{PT::kVoiceEngineNoteE1, PT::kVoiceEngineNoteA1, PT::kVoiceEngineNoteE2,
                                           PT::kVoiceEngineNoteC3};
    notes.clear();
    for (std::size_t i = 0; i < keys.size(); ++i) {
        const std::uint64_t at = veAt(0.03 * static_cast<double>(i), kVeFs);
        sc.events.push_back(veOn(at, keys[i]));
        notes.push_back(VeRefNote{.sample = at,
                                  .note = keys[i],
                                  .transition = NoteMemory::Transition::FromSilence,
                                  .audible = false});
    }
    veAddBendRamp(sc.events, veAt(0.12, kVeFs), veAt(0.32, kVeFs));
    return sc;
}

[[nodiscard]] const char* veAnchorModeName(AnchorOscillator::Mode m) {
    switch (m) {
        case AnchorOscillator::Mode::Lock: return "LOCK";
        case AnchorOscillator::Mode::Follow: return "FOLLOW";
        case AnchorOscillator::Mode::Punch: return "PUNCH";
        case AnchorOscillator::Mode::Ghost: return "GHOST";
    }
    return "?";
}

/// One positive-control render (MONO): a seam on, the reference unchanged; returns the Anchor gate's mismatch count.
[[nodiscard]] std::size_t veDropControl(AnchorOscillator::Mode mode, bool followNoBypass, bool lockReadsCore) {
    std::vector<VeRefNote> notes;
    const MorphState s = veDropState(mode);
    PT::VoiceEngineScript sc = veDropMonoScript(s, notes);
    sc.config.seams.followNoBypass = followNoBypass;
    sc.config.seams.lockReadsCore = lockReadsCore;
    const PT::VoiceEngineTaps t = PT::voiceEngineRender(sc);
    const std::vector<VeRefTrack> ref = veDropReference(sc.fs, t.total, true, s, kVeGlideMs,
                                                        ProfundumEngine::kDefaultBendRangeSt, notes, sc.events);
    VePitchGate g;
    veGatePitch(t.voices[0], ref[0], mode == AnchorOscillator::Mode::Follow, t.total, g);
    std::cout << std::setprecision(6) << "VoiceEngine_DropRouting control "
              << (followNoBypass ? "followNoBypass" : "lockReadsCore") << " (" << veAnchorModeName(mode)
              << "): Anchor mismatches " << g.anchorMismatch << " (max " << g.maxAnchorCents << " cent)\n";
    return g.anchorMismatch;
}

// ---- SC-033 ---------------------------------------------------------------------------------------------------

/// h2 / h1 in dB of the mid of a voice's Character tap over the last 8 periods before `end` (single-bin DFTs in double
/// at f and 2f, f = the core pitch tap). Character Amount 0 is the stage's bypass copy (bass_character_stage.h
/// processBlock, step 3), so this is the Character input: the Fold output mid the voice's meter reads.
[[nodiscard]] double veH2H1Db(const PT::VoiceEngineVoiceTapSet& v, std::size_t end, double fs) {
    REQUIRE(end > 0);
    const auto f = static_cast<double>(v.fCore[end - 1]);
    REQUIRE(f > 0.0);
    const auto len = static_cast<std::size_t>(std::llround(8.0 * fs / f));
    REQUIRE(len < end);
    const std::size_t start = end - len;
    double r1 = 0.0;
    double i1 = 0.0;
    double r2 = 0.0;
    double i2 = 0.0;
    for (std::size_t k = 0; k < len; ++k) {
        const double x = 0.5 * (static_cast<double>(v.charL[start + k]) + static_cast<double>(v.charR[start + k]));
        const double ph = 2.0 * kVePi * f * static_cast<double>(k) / fs;
        r1 += x * std::cos(ph);
        i1 -= x * std::sin(ph);
        r2 += x * std::cos(2.0 * ph);
        i2 -= x * std::sin(2.0 * ph);
    }
    const double m1 = std::max(std::sqrt(r1 * r1 + i1 * i1), 1e-30);
    const double m2 = std::max(std::sqrt(r2 * r2 + i2 * i2), 1e-30);
    return 20.0 * std::log10(m2 / m1);
}

constexpr double kVeRectSeconds = 0.6;

/// The steady h2 / h1 of a state: MONO, E1 held, 0.6 s, read over the last 8 periods.
[[nodiscard]] double veMeasureH2H1(const MorphState& s) {
    PT::VoiceEngineScript sc = veScript(s, kVeRectSeconds, VeMode::Mono, 1);
    sc.events = {veOn(0, PT::kVoiceEngineNoteE1)};
    const PT::VoiceEngineTaps t = PT::voiceEngineRender(sc);
    return veH2H1Db(t.voices[0], t.total, sc.fs);
}

/// The knobs that place a state's h2 / h1, tried in order: the task's Fold amount, then excitation asymmetry; the
/// recipe depth (sine -> saw) is a recorded fallback, because on the A_s sine core neither of the first two is certain
/// to produce an h2 (run-log T010).
struct VeKnob {
    const char* name;
    float lo;
    float hi;
    void (*apply)(MorphState&, float);
};

void veKnobFold(MorphState& s, float x) { s.foldAmount = x; }
void veKnobAsymmetry(MorphState& s, float x) {
    s.excitation.mix = 1.0f;
    s.excitation.drive = 1.0f;
    s.excitation.asymmetry = x;
}
void veKnobDepth(MorphState& s, float x) { s.recipe.depth = x; }

const std::array<VeKnob, 3> kVeKnobs{VeKnob{"foldAmount", 0.0f, 100.0f, &veKnobFold},
                                     VeKnob{"excitation.asymmetry (mix 1, drive 1)", 0.0f, 1.0f, &veKnobAsymmetry},
                                     VeKnob{"recipe.depth (fallback)", 0.0f, 1.0f, &veKnobDepth}};

struct VeRectState {
    double targetDb = 0.0;
    MorphState state{};
    double measuredDb = 0.0;
    const char* knob = "none";
    float x = 0.0f;
    bool found = false;
};

/// SC-033's states: each target placed within +-0.5 dB by a 9-point scan of each knob in turn and a bisection inside
/// the first bracketing pair (A_s base: Character Amount 0, SOLID, Drop 0).
[[nodiscard]] std::vector<VeRectState> veFindRectStates(const std::vector<double>& targets) {
    std::vector<VeRectState> out(targets.size());
    for (std::size_t i = 0; i < targets.size(); ++i)
        out[i].targetDb = targets[i];
    constexpr std::size_t kScan = 9;
    for (const VeKnob& knob : kVeKnobs) {
        bool all = true;
        for (const VeRectState& r : out)
            all = all && r.found;
        if (all)
            break;
        std::array<float, kScan> xs{};
        std::array<double, kScan> vs{};
        for (std::size_t i = 0; i < kScan; ++i) {
            xs[i] = knob.lo + (knob.hi - knob.lo) * static_cast<float>(i) / static_cast<float>(kScan - 1);
            MorphState s = veStateAs();
            knob.apply(s, xs[i]);
            vs[i] = veMeasureH2H1(s);
            std::cout << std::setprecision(6) << "VoiceEngine_RectifySymmetry scan " << knob.name << " = " << xs[i]
                      << ": h2/h1 " << vs[i] << " dB\n";
        }
        for (VeRectState& r : out) {
            if (r.found)
                continue;
            const double T = r.targetDb;
            for (std::size_t i = 0; i + 1 < kScan; ++i) {
                if ((vs[i] - T) * (vs[i + 1] - T) > 0.0)
                    continue;
                float lo = xs[i];
                float hi = xs[i + 1];
                double vLo = vs[i];
                const bool loCloser = std::abs(vs[i] - T) < std::abs(vs[i + 1] - T);
                double best = loCloser ? vs[i] : vs[i + 1];
                float bestX = loCloser ? lo : hi;
                for (int it = 0; it < 16 && std::abs(best - T) > 0.25; ++it) {
                    const float mid = 0.5f * (lo + hi);
                    MorphState s = veStateAs();
                    knob.apply(s, mid);
                    const double v = veMeasureH2H1(s);
                    if (std::abs(v - T) < std::abs(best - T)) {
                        best = v;
                        bestX = mid;
                    }
                    if ((vLo - T) * (v - T) <= 0.0) {
                        hi = mid;
                    } else {
                        lo = mid;
                        vLo = v;
                    }
                }
                MorphState s = veStateAs();
                knob.apply(s, bestX);
                r.state = s;
                r.measuredDb = best;
                r.knob = knob.name;
                r.x = bestX;
                r.found = std::abs(best - T) <= 0.5;
                break;
            }
        }
    }
    return out;
}

/// MONO, E1 held: `from` for segA, then endpoint A := `to` at a grid point (position 0, so the resolved state is `to`
/// from that grid point on), for segB.
[[nodiscard]] PT::VoiceEngineTaps veSwitchRender(const MorphState& from, const MorphState& to, double segA,
                                                 double segB, std::uint64_t& swapAt) {
    PT::VoiceEngineScript sc = veScript(from, segA + segB, VeMode::Mono, 1);
    swapAt = veAt(segA, kVeFs);
    sc.endpointStates = {to};
    sc.events = {veOn(0, PT::kVoiceEngineNoteE1), veEv(swapAt, VeKind::Endpoint, 0.0f, 0)};
    return PT::voiceEngineRender(sc);
}

/// Chunks of a voice starting in [from, to) whose handed HalfWave flag differs from `expected`; `chunks` counts all.
[[nodiscard]] std::size_t veFlagMisses(const PT::VoiceEngineVoiceTapSet& v, std::uint64_t from, std::uint64_t to,
                                       bool expected, std::size_t& chunks) {
    std::size_t misses = 0;
    chunks = 0;
    for (const PT::VoiceEngineChunkRecord& c : v.chunks) {
        if (c.sample < from || c.sample >= to)
            continue;
        ++chunks;
        if (c.halfWave != expected)
            ++misses;
    }
    return misses;
}

[[nodiscard]] std::uint64_t vePeriods(double periods, double f, double fs) {
    return static_cast<std::uint64_t>(std::llround(periods * fs / f));
}

}  // namespace

// SC-019 (H-5, H-7; FR-003, FR-004, FR-015; plan C-10, S10.6): the join point and the plain add, through the
// engine.
TEST_CASE("VoiceEngine_AnchorJoin", "[systems][profundum][voice-engine][sc-019]") {
    enableFTZDAZ();
    const std::filesystem::path dir = veEvidenceDir();
    std::filesystem::create_directories(dir);
    std::ofstream ev(dir / "sc019-join.txt");
    ev << "SC-019 VoiceEngine_AnchorJoin (profundum_voice_test.cpp, T010). fs 48000, A_s (C-8), Anchor level 1.\n"
       << "(b) per render: samples, Sum-identity mismatches, single-voice samples, intervals with g_d != 1 (their grid "
          "samples listed).\n";

    // (a) g = 0: anchorDetached vs attached, E1 held 2 s: the core and Character taps are bitwise equal.
    {
        PT::VoiceEngineScript att = veScript(veWithFeed(0.0f), 2.0, VeMode::Mono, 1);
        att.events = {veOn(0, PT::kVoiceEngineNoteE1)};
        PT::VoiceEngineScript det = att;
        det.config.seams.anchorDetached = true;
        const PT::VoiceEngineTaps ta = PT::voiceEngineRender(att);
        const PT::VoiceEngineTaps td = PT::voiceEngineRender(det);
        const PT::VoiceEngineVoiceTapSet& a = ta.voices[0];
        const PT::VoiceEngineVoiceTapSet& d = td.voices[0];
        std::size_t mism = 0;
        for (std::size_t i = 0; i < ta.total; ++i) {
            if (!veSameBits(a.coreL[i], d.coreL[i]) || !veSameBits(a.coreR[i], d.coreR[i])
                || !veSameBits(a.charL[i], d.charL[i]) || !veSameBits(a.charR[i], d.charR[i]))
                ++mism;
        }
        std::cout << "VoiceEngine_AnchorJoin (a): samples " << ta.total << ", core / Character mismatches " << mism
                  << "\n";
        ev << "(a) detached vs attached at g = 0, E1 2 s: samples " << ta.total << ", core/Character mismatches "
           << mism << "\n";
        CHECK_FALSE(a.chunks.empty());
        CHECK(mism == 0);
    }

    // (b) at g in {0, 0.5, 1}: a MONO Reset retrigger (a dip) and a POLY steal (a dip; the stolen voice alone later).
    std::vector<float> anchorG0L, anchorG0R, anchorG1L, anchorG1R;
    for (const float g : {0.0f, 0.5f, 1.0f}) {
        INFO("g " << g);
        const MorphState s = veWithFeed(g);
        const PT::VoiceEngineTaps tm = PT::voiceEngineRender(veMonoRetriggerScript(s));
        const PT::VoiceEngineTaps tp = PT::voiceEngineRender(vePolyStealScript(s));
        const VeJoin jm = veJoinIdentity(tm, 1);
        const VeJoin jp = veJoinIdentity(tp, ProfundumEngine::kDefaultPolyphony);
        veWriteJoinLine(ev, "MONO retrigger", g, tm, jm);
        veWriteJoinLine(ev, "POLY steal", g, tp, jp);
        CHECK(jm.mismatches == 0);
        CHECK(jp.mismatches == 0);
        CHECK(jm.dipGrids.size() >= 1);
        CHECK(jp.dipGrids.size() >= 1);
        CHECK(tm.droppedEvents == 0);
        CHECK(tp.droppedEvents == 0);
        // The stolen voice is read alone after the others have released (0.55 ... 1 s).
        CHECK(jp.singleVoiceSamples > static_cast<std::size_t>(0.3 * kVeFs));
        if (veSameBits(g, 0.0f)) {
            anchorG0L = tm.voices[0].anchorL;
            anchorG0R = tm.voices[0].anchorR;
        }
        if (veSameBits(g, 1.0f)) {
            anchorG1L = tm.voices[0].anchorL;
            anchorG1R = tm.voices[0].anchorR;
        }
    }

    // (c) at g = 1 the direct Anchor path is unchanged by g: the Anchor tap equals the g = 0 render's.
    {
        REQUIRE(anchorG0L.size() == anchorG1L.size());
        REQUIRE_FALSE(anchorG0L.empty());
        std::size_t mism = 0;
        for (std::size_t i = 0; i < anchorG0L.size(); ++i) {
            if (!veSameBits(anchorG0L[i], anchorG1L[i]) || !veSameBits(anchorG0R[i], anchorG1R[i]))
                ++mism;
        }
        std::cout << "VoiceEngine_AnchorJoin (c): Anchor tap g = 1 vs g = 0 mismatches " << mism << "\n";
        ev << "(c) Anchor tap at g = 1 vs g = 0 (MONO retrigger render): mismatches " << mism << "\n";
        CHECK(mism == 0);
    }

    // (b) across an interval where g steps 0 -> 1 (endpoint A replaced at a grid point), and (d) the feed ramp.
    {
        PT::VoiceEngineScript sc = veMonoRetriggerScript(veWithFeed(0.0f));
        const std::uint64_t stepAt = veAt(0.5, kVeFs);
        sc.endpointStates = {veWithFeed(1.0f)};
        sc.events.push_back(veEv(stepAt, VeKind::Endpoint, 0.0f, 0));
        const PT::VoiceEngineTaps t = PT::voiceEngineRender(sc);
        const VeJoin j = veJoinIdentity(t, 1);
        veWriteJoinLine(ev, "MONO retrigger, g step 0 -> 1 at 0.5 s", 0.0f, t, j);
        CHECK(j.mismatches == 0);
        CHECK(j.dipGrids.size() >= 1);

        // (d) S9.5: feed[k] = from + (to - from) * float(k + 1) / 32.0f at interval position k; +0 while both ends +0.
        const PT::VoiceEngineVoiceTapSet& v = t.voices[0];
        std::size_t rampSamples = 0;
        std::size_t rampMismatch = 0;
        std::size_t skippedSamples = 0;
        std::size_t skippedNonZero = 0;
        bool landed = false;
        for (const PT::VoiceEngineChunkRecord& c : v.chunks) {
            const bool fromZero = std::bit_cast<std::uint32_t>(c.feedFrom) == 0u;
            const bool toZero = std::bit_cast<std::uint32_t>(c.feedTo) == 0u;
            for (std::size_t i = 0; i < c.n; ++i) {
                const auto n = static_cast<std::size_t>(c.sample) + i;
                if (n >= t.total)
                    break;
                if (fromZero && toZero) {
                    ++skippedSamples;
                    if (std::bit_cast<std::uint32_t>(v.feed[n]) != 0u)
                        ++skippedNonZero;
                } else if (fromZero && veSameBits(c.feedTo, 1.0f)) {
                    const auto k = static_cast<std::size_t>(n % kVeGrid);
                    const float want = c.feedFrom + (c.feedTo - c.feedFrom) * static_cast<float>(k + 1) / 32.0f;
                    ++rampSamples;
                    if (!veSameBits(v.feed[n], want))
                        ++rampMismatch;
                    if (k == kVeGrid - 1 && veSameBits(v.feed[n], 1.0f))
                        landed = true;
                }
            }
        }
        std::cout << "VoiceEngine_AnchorJoin (d): ramp samples " << rampSamples << ", ramp mismatches " << rampMismatch
                  << ", skipped samples " << skippedSamples << ", skipped non-+0 " << skippedNonZero << "\n";
        ev << "(d) feed ramp through the engine: ramp samples " << rampSamples << ", mismatches " << rampMismatch
           << ", landed on 1.0f " << (landed ? "yes" : "no") << "; skipped (+0) samples " << skippedSamples
           << ", non-+0 " << skippedNonZero << "\n";
        CHECK(rampSamples == kVeGrid);
        CHECK(rampMismatch == 0);
        CHECK(landed);
        CHECK(skippedSamples > 0);
        CHECK(skippedNonZero == 0);
    }
}

// SC-020 (H-5; FR-006; plan C-11): the Drop drives the core pitch only; the Anchor reads f_res except in FOLLOW. Gated
// bitwise against an independent test-side reference; the max cent difference is printed (expected 0).
TEST_CASE("VoiceEngine_DropRouting", "[systems][profundum][voice-engine][sc-020][fr-006]") {
    enableFTZDAZ();
    const std::array<AnchorOscillator::Mode, 4> modes{AnchorOscillator::Mode::Lock, AnchorOscillator::Mode::Punch,
                                                      AnchorOscillator::Mode::Ghost, AnchorOscillator::Mode::Follow};
    for (const AnchorOscillator::Mode mode : modes) {
        const bool follow = mode == AnchorOscillator::Mode::Follow;
        const MorphState s = veDropState(mode);
        for (const bool mono : {true, false}) {
            INFO(veAnchorModeName(mode) << (mono ? " MONO" : " POLY"));
            std::vector<VeRefNote> notes;
            const PT::VoiceEngineScript sc = mono ? veDropMonoScript(s, notes) : veDropPolyScript(s, notes);
            const PT::VoiceEngineTaps t = PT::voiceEngineRender(sc);
            const std::vector<VeRefTrack> ref =
                veDropReference(sc.fs, t.total, mono, s, mono ? kVeGlideMs : 0.0f,
                                ProfundumEngine::kDefaultBendRangeSt, notes, sc.events);
            VePitchGate g;
            for (std::size_t tr = 0; tr < ref.size(); ++tr) {
                const std::size_t v = mono ? std::size_t{0} : veVoiceStartingAt(t, notes[tr].sample);
                REQUIRE(v < t.tappedVoices);
                REQUIRE(ref[tr].start < t.total);
                veGatePitch(t.voices[v], ref[tr], follow, t.total, g);
            }
            std::cout << std::setprecision(6) << "VoiceEngine_DropRouting " << veAnchorModeName(mode)
                      << (mono ? " MONO" : " POLY") << ": compared " << g.compared << ", core mismatches "
                      << g.coreMismatch << " (max " << g.maxCoreCents << " cent), Anchor mismatches "
                      << g.anchorMismatch << " (max " << g.maxAnchorCents << " cent), f_res mismatches "
                      << g.resMismatch << "\n";
            CHECK(g.compared > static_cast<std::size_t>(0.4 * kVeFs));
            CHECK(g.resMismatch == 0);
            CHECK(g.coreMismatch == 0);
            CHECK(g.anchorMismatch == 0);
        }
    }

    // Positive controls (MONO): each seam must break the Anchor pitch gate.
    const bool followNoBypassAllEqual = veDropControl(AnchorOscillator::Mode::Follow, true, false) == 0;
    const bool lockReadsCoreAllEqual = veDropControl(AnchorOscillator::Mode::Lock, false, true) == 0;
    CHECK_FALSE(followNoBypassAllEqual);
    CHECK_FALSE(lockReadsCoreAllEqual);
}

// FR-006: the Drop parameters latch at the trigger. A mid-Drop morph (5 ms ramp, 50 ms after the note) to Drop Amount
// 12 / Time 60 / Shape 1 / Rebound 1 with CYCLE -> Depth active leaves the sounding note's f_core bitwise unchanged;
// the next note's Drop follows the new values.
TEST_CASE("VoiceEngine_DropLatchedAtTrigger", "[systems][profundum][voice-engine][fr-006]") {
    enableFTZDAZ();
    MorphState a = veDropState(AnchorOscillator::Mode::Lock);
    a.dropShape = 0.0f;
    a.dropRebound = 0.0f;
    MorphState b = a;
    b.dropAmountSt = 12.0f;
    b.dropTimeMs = 60.0f;
    b.dropShape = 1.0f;
    b.dropRebound = 1.0f;
    b.motion.source = MorphState::MotionConfig::Source::Cycle;
    b.motion.target = MorphState::MotionConfig::Target::Depth;
    b.motion.amount = 1.0f;
    b.motion.depth = 1.0f;
    REQUIRE(Krate::DSP::isValidMorphState(b));

    const std::uint64_t morphAt = veAt(0.05, kVeFs);
    const std::uint64_t offAt = veAt(0.4, kVeFs);
    const std::uint64_t nextAt = veAt(0.6, kVeFs);
    const auto build = [&](bool morph) {
        PT::VoiceEngineScript sc = veScript(a, 1.0, VeMode::Mono, 1);
        sc.b = b;
        sc.events = {veOn(0, PT::kVoiceEngineNoteE1), veOff(offAt, PT::kVoiceEngineNoteE1),
                     veOn(nextAt, PT::kVoiceEngineNoteE1)};
        if (morph) {
            sc.events.push_back(veEv(0, VeKind::MorphRampMs, 5.0f));
            sc.events.push_back(veEv(morphAt, VeKind::MorphPosition, 1.0f));
        }
        return sc;
    };
    const PT::VoiceEngineTaps still = PT::voiceEngineRender(build(false));
    const PT::VoiceEngineTaps moved = PT::voiceEngineRender(build(true));

    // The change reached the state: the B Drop fields and a running CYCLE after the ramp.
    REQUIRE_FALSE(moved.grid.empty());
    CHECK(veSameBits(moved.grid.back().afterMotion.dropAmountSt, 12.0f));
    CHECK(moved.grid.back().afterMotion.motion.source == MorphState::MotionConfig::Source::Cycle);
    std::size_t motionNonZero = 0;
    for (const PT::VoiceEngineGridRecord& r : moved.grid) {
        if (r.sample >= veAt(0.1, kVeFs) && std::bit_cast<std::uint32_t>(r.motionOutput) != 0u)
            ++motionNonZero;
    }
    CHECK(motionNonZero > 0);

    const PT::VoiceEngineVoiceTapSet& v0 = still.voices[0];
    const PT::VoiceEngineVoiceTapSet& v1 = moved.voices[0];
    std::size_t sameNoteMismatch = 0;
    for (std::size_t i = 0; i < static_cast<std::size_t>(nextAt); ++i) {
        if (!veSameBits(v0.fCore[i], v1.fCore[i]))
            ++sameNoteMismatch;
    }
    double nextMaxCents = 0.0;
    const auto nextEnd = static_cast<std::size_t>(nextAt + veAt(0.25, kVeFs));
    for (auto i = static_cast<std::size_t>(nextAt); i < nextEnd && i < moved.total; ++i) {
        if (v0.fCore[i] > 0.0f && v1.fCore[i] > 0.0f)
            nextMaxCents = std::max(nextMaxCents, std::abs(veCents(v1.fCore[i], v0.fCore[i])));
    }
    std::cout << std::setprecision(6) << "VoiceEngine_DropLatchedAtTrigger: sounding-note f_core mismatches "
              << sameNoteMismatch << ", next-note max |delta f_core| " << nextMaxCents << " cent\n";
    CHECK(sameNoteMismatch == 0);
    CHECK(nextMaxCents > 1.0);
}

// SC-033 (H-11; FR-008; plan C-5): the HalfWave flag follows the measured Character-input h2 / h1 with hysteresis
// inside -22 ... -18 dB, and syncPhase(0) is issued exactly at the Reset-branch samples, where the core's h1 phase
// equals a fresh core's.
TEST_CASE("VoiceEngine_RectifySymmetry", "[systems][profundum][voice-engine][sc-033][fr-008]") {
    enableFTZDAZ();
    const auto fE1 = static_cast<double>(Krate::DSP::midiNoteToFrequency(static_cast<int>(PT::kVoiceEngineNoteE1)));

    // States at -30, -25, -20, -15, -10 dB (each within +-0.5 dB).
    const std::vector<VeRectState> st = veFindRectStates({-30.0, -25.0, -20.0, -15.0, -10.0});
    for (const VeRectState& r : st) {
        std::cout << std::setprecision(6) << "VoiceEngine_RectifySymmetry state " << r.targetDb << " dB: "
                  << (r.found ? "found" : "NOT FOUND") << " via " << r.knob << " = " << r.x << ", measured "
                  << r.measuredDb << " dB\n";
    }
    for (const VeRectState& r : st) {
        INFO("target " << r.targetDb << " dB, knob " << r.knob << ", measured " << r.measuredDb);
        REQUIRE(r.found);
    }
    const VeRectState& s30 = st[0];
    const VeRectState& s25 = st[1];
    const VeRectState& s20 = st[2];
    const VeRectState& s15 = st[3];
    const VeRectState& s10 = st[4];

    // The flag at each state, reached from either previous flag value (a true state: -30; a false state: -10).
    const std::uint64_t last4 = vePeriods(4.0, fE1, kVeFs);
    for (const VeRectState* target : {&s30, &s25, &s15, &s10}) {
        const bool want = target->targetDb < -20.0;
        for (const VeRectState* from : {&s30, &s10}) {
            const bool fromFlag = from->targetDb < -20.0;
            INFO("to " << target->targetDb << " dB from " << from->targetDb << " dB");
            std::uint64_t swapAt = 0;
            const PT::VoiceEngineTaps t = veSwitchRender(from->state, target->state, 0.4, 0.6, swapAt);
            std::size_t chunksA = 0;
            std::size_t chunksB = 0;
            const std::size_t missA = veFlagMisses(t.voices[0], swapAt - last4, swapAt, fromFlag, chunksA);
            const std::size_t missB = veFlagMisses(t.voices[0], t.total - last4, t.total, want, chunksB);
            std::cout << "VoiceEngine_RectifySymmetry flag " << from->targetDb << " -> " << target->targetDb
                      << " dB: before the switch misses " << missA << " / " << chunksA << ", at the end misses "
                      << missB << " / " << chunksB << " (want " << (want ? "true" : "false") << ")\n";
            CHECK(chunksA > 0);
            CHECK(chunksB > 0);
            CHECK(missA == 0);
            CHECK(missB == 0);
        }
    }

    // Hysteresis: -20 dB reached from -25 keeps true, reached from -15 keeps false, over >= 20 periods.
    for (const VeRectState* from : {&s25, &s15}) {
        const bool keep = from->targetDb < -20.0;
        INFO("-20 dB from " << from->targetDb << " dB");
        std::uint64_t swapAt = 0;
        const PT::VoiceEngineTaps t = veSwitchRender(from->state, s20.state, 0.4, 0.6, swapAt);
        REQUIRE(t.total - swapAt >= vePeriods(20.0, fE1, kVeFs));
        std::size_t chunksA = 0;
        const std::size_t missA = veFlagMisses(t.voices[0], swapAt - last4, swapAt, keep, chunksA);
        std::size_t chunks = 0;
        const std::size_t miss = veFlagMisses(t.voices[0], swapAt, t.total, keep, chunks);
        std::cout << "VoiceEngine_RectifySymmetry hysteresis " << from->targetDb << " -> -20 dB: misses " << miss
                  << " / " << chunks << " chunks over " << (t.total - swapAt) << " samples (keep "
                  << (keep ? "true" : "false") << ")\n";
        CHECK(chunksA > 0);
        CHECK(missA == 0);
        CHECK(chunks > 0);
        CHECK(miss == 0);
    }

    // Phase sync (FR-008): SyncPhase issued exactly at the Reset-branch effective samples and the first note after
    // prepare; at each, the core's h1 phase over 4 periods equals a fresh prepared core's within 1 degree.
    RetriggerDeclick dk;
    dk.prepare(kVeFs, ProfundumVoice::kDeclickMs);   // R-B
    dk.arm();
    const auto untilSwitch = static_cast<std::uint64_t>(dk.samplesUntilSwitch());
    const std::uint64_t a1On = veAt(0.3, kVeFs);
    const std::uint64_t a1Off = veAt(0.5, kVeFs);
    const std::uint64_t e1Off = veAt(0.7, kVeFs);
    const std::uint64_t e2On = veAt(1.2, kVeFs);
    for (const NoteMemory::Retrigger policy : {NoteMemory::Retrigger::Reset, NoteMemory::Retrigger::Continue}) {
        const bool reset = policy == NoteMemory::Retrigger::Reset;
        INFO((reset ? "Retrigger Reset" : "Retrigger Continue"));
        PT::VoiceEngineScript sc = veScript(veStateAs(), 1.6, VeMode::Mono, 1);
        sc.events = {veEv(0, VeKind::NoteRetrigger, 0.0f, static_cast<int>(policy)),
                     veOn(0, PT::kVoiceEngineNoteE1),
                     veOn(a1On, PT::kVoiceEngineNoteA1),     // Overlap, audible
                     veOff(a1Off, PT::kVoiceEngineNoteA1),   // ReturnToHeld (E1), audible
                     veOff(e1Off, PT::kVoiceEngineNoteE1),
                     veOn(e2On, PT::kVoiceEngineNoteE2)};    // FromSilence (the 50 ms release has ended)
        const PT::VoiceEngineTaps t = PT::voiceEngineRender(sc);
        // AlwaysRetrigger restarts the oscillator on every transition. Under Reset the phase resets (an audible voice
        // at the dip's switch sample); under Continue only the first note after prepare takes the Reset branch.
        const std::vector<std::uint64_t> expected =
            reset ? std::vector<std::uint64_t>{0, a1On + untilSwitch, a1Off + untilSwitch, e2On}
                  : std::vector<std::uint64_t>{0};
        std::vector<std::uint64_t> got;
        for (const ProfundumCallRecord& r : t.calls) {
            if (r.voice == 0 && r.call == ProfundumCall::SyncPhase && r.issued)
                got.push_back(r.sample);
        }
        std::cout << "VoiceEngine_RectifySymmetry sync (" << (reset ? "Reset" : "Continue") << "): issued at";
        for (const std::uint64_t x : got)
            std::cout << " " << x;
        std::cout << "; expected";
        for (const std::uint64_t x : expected)
            std::cout << " " << x;
        std::cout << "\n";
        CHECK(got == expected);

        const PT::VoiceEngineVoiceTapSet& v = t.voices[0];
        for (const std::uint64_t at : expected) {
            const auto s0 = static_cast<std::size_t>(at);
            const float f = v.fCore[s0];
            REQUIRE(f > 0.0f);
            const auto len = static_cast<std::size_t>(std::llround(4.0 * kVeFs / static_cast<double>(f)));
            REQUIRE(s0 + len <= t.total);
            const std::size_t gi = s0 / static_cast<std::size_t>(kVeGrid);
            REQUIRE(gi < t.grid.size());
            const PT::VoiceEngineGridRecord& gr = t.grid[gi];
            REQUIRE(gr.sample == static_cast<std::uint64_t>(gi) * kVeGrid);
            SpectralShapeRecipe::Controls c = gr.afterMotion.recipe;   // the handed core controls (plan S6.5)
            c.depth = gr.targets.depth;
            c.body = gr.targets.body;
            auto core = std::make_unique<ProfundumCore>();
            core->prepare(kVeFs, ProfundumCore::kDefaultPartials);
            core->setControls(c);
            core->setExcitation(gr.afterMotion.excitation);
            core->setRetriggerPhase(ProfundumCore::RetriggerPhase::Reset);
            core->noteOn(f);
            std::vector<float> fl(len, 0.0f);
            std::vector<float> fr(len, 0.0f);
            core->processBlock(fl.data(), fr.data(), len, v.fCore.data() + s0);
            std::vector<float> midV(len, 0.0f);
            std::vector<float> midF(len, 0.0f);
            for (std::size_t k = 0; k < len; ++k) {
                midV[k] = 0.5f * (v.coreL[s0 + k] + v.coreR[s0 + k]);
                midF[k] = 0.5f * (fl[k] + fr[k]);
            }
            const double pv = PT::goertzelPhase(midV.data(), len, kVeFs, static_cast<double>(f));
            const double pf = PT::goertzelPhase(midF.data(), len, kVeFs, static_cast<double>(f));
            const double diffDeg = std::abs(std::remainder(pv - pf, 2.0 * kVePi)) * 180.0 / kVePi;
            std::cout << std::setprecision(6) << "VoiceEngine_RectifySymmetry sync phase at " << at << " (f " << f
                      << " Hz): voice " << pv << " rad, fresh core " << pf << " rad, |diff| " << diffDeg << " deg\n";
            CHECK(diffDeg <= 1.0);
        }
    }
}

// SC-034 (FR-024): a full bend moves f_res by the bend range within 0.1 cent, in MONO and in POLY (each voice).
TEST_CASE("VoiceEngine_PitchBend", "[systems][profundum][voice-engine][sc-034][fr-024]") {
    enableFTZDAZ();
    const std::uint64_t baseAt = veAt(0.09, kVeFs);
    const std::uint64_t upOn = veAt(0.1, kVeFs);
    const std::uint64_t upAt = veAt(0.45, kVeFs);
    const std::uint64_t downOn = veAt(0.5, kVeFs);
    const std::uint64_t downAt = veAt(0.9, kVeFs);
    const std::array<std::uint8_t, 4> keys{PT::kVoiceEngineNoteE1, PT::kVoiceEngineNoteA1, PT::kVoiceEngineNoteE2,
                                           PT::kVoiceEngineNoteC3};
    for (const float range : {2.0f, 12.0f}) {
        for (const bool mono : {true, false}) {
            INFO("range " << range << (mono ? " MONO" : " POLY"));
            PT::VoiceEngineScript sc = veScript(veStateAs(), 1.0, mono ? VeMode::Mono : VeMode::Poly,
                                                mono ? std::size_t{1} : ProfundumEngine::kDefaultPolyphony);
            sc.polyphony = ProfundumEngine::kDefaultPolyphony;
            if (mono) {
                sc.events.push_back(veOn(0, PT::kVoiceEngineNoteE1));
            } else {
                for (const std::uint8_t k : keys)
                    sc.events.push_back(veOn(0, k));
            }
            sc.events.push_back(veEv(upOn, VeKind::PitchBend, 1.0f));
            sc.events.push_back(veEv(downOn, VeKind::PitchBend, -1.0f));
            const PT::VoiceEngineTaps t = veRenderWithBendRange(sc, range);
            std::size_t voices = 0;
            std::size_t matchedNotes = 0;
            for (std::size_t v = 0; v < t.tappedVoices; ++v) {
                const PT::VoiceEngineVoiceTapSet& p = t.voices[v];
                if (p.chunks.empty())
                    continue;
                ++voices;
                const auto base = static_cast<double>(p.fRes[baseAt]);
                const auto up = static_cast<double>(p.fRes[upAt]);
                const auto down = static_cast<double>(p.fRes[downAt]);
                REQUIRE(base > 0.0);
                if (!mono) {
                    for (const std::uint8_t k : keys) {
                        if (veSameBits(p.fRes[baseAt], Krate::DSP::midiNoteToFrequency(static_cast<int>(k))))
                            ++matchedNotes;
                    }
                }
                const double errUp = std::abs(veCents(up, base) - 100.0 * static_cast<double>(range));
                const double errDown = std::abs(veCents(down, base) + 100.0 * static_cast<double>(range));
                std::cout << std::setprecision(6) << "VoiceEngine_PitchBend range " << range
                          << (mono ? " MONO" : " POLY") << " voice " << v << " (" << base << " Hz): +1 error "
                          << errUp << " cent, -1 error " << errDown << " cent\n";
                CHECK(errUp <= 0.1);
                CHECK(errDown <= 0.1);
            }
            CHECK(voices == (mono ? std::size_t{1} : keys.size()));
            if (!mono)
                CHECK(matchedNotes == keys.size());
        }
    }
}

// FR-008 / C-5 (plan S9.6): note 127, bend range 24, bend +1 puts f_res far above fs / 2. Through the engine, per
// chunk: theta in [0, 1); the meter frequency is the core's clamp of f_core at the chunk's last 16-sample pitch update
// (the T007 form), never above capHz, and == capHz once the bend smoother and the note's lock bump have settled (100
// ms; ProfundumVoice_MeterBounded found the lock bump keeps f_core a few cents off the cap for ~8 chunks); the output
// is finite; and no HalfWave flip is handed while the meter's period is shorter than 2 samples.
TEST_CASE("VoiceEngine_MeterBoundedAtTopNote", "[systems][profundum][voice-engine][fr-008][c-5]") {
    enableFTZDAZ();
    for (const double fs : {44100.0, 48000.0}) {
        INFO("fs " << fs);
        PT::VoiceEngineScript sc = veScript(veStateAs(), 2.0, VeMode::Mono, 1, fs);
        sc.events = {veEv(0, VeKind::PitchBend, 1.0f), veOn(0, 127)};
        const PT::VoiceEngineTaps t = veRenderWithBendRange(sc, ProfundumEngine::kMaxBendRangeSt);
        const PT::VoiceEngineVoiceTapSet& v = t.voices[0];
        const float capHz = SpectralShapeRecipe::capFrequency(fs);
        constexpr std::uint64_t kU = ProfundumCore::kPitchUpdateInterval;
        const std::uint64_t settle = veAt(0.1, fs);
        std::size_t thetaOut = 0;
        std::size_t freqOff = 0;
        std::size_t aboveCap = 0;
        std::size_t offCapAfterSettle = 0;
        for (const PT::VoiceEngineChunkRecord& c : v.chunks) {
            if (c.meterTheta < 0.0f || c.meterTheta >= 1.0f)
                ++thetaOut;
            std::size_t last = 0;   // the note-on chunk's offset 0, else the chunk's last 16-sample pitch update
            for (std::size_t i = 0; i < c.n; ++i) {
                if ((c.sample + i) % kU == 0u)
                    last = i;
            }
            const float expected =
                std::clamp(v.fCore[static_cast<std::size_t>(c.sample) + last], ProfundumCore::kMinF0Hz, capHz);
            if (!veSameBits(c.meterFreqHz, expected))
                ++freqOff;
            if (c.meterFreqHz > capHz)
                ++aboveCap;
            if (c.sample >= settle && !veSameBits(c.meterFreqHz, capHz))
                ++offCapAfterSettle;
        }
        std::size_t nonFinite = 0;
        for (std::size_t i = 0; i < t.total; ++i) {
            if (!Krate::DSP::detail::isFinite(t.outL[i]) || !Krate::DSP::detail::isFinite(t.outR[i]))
                ++nonFinite;
        }
        // HalfWave changes handed after the first interval: the meter frequency of the chunk before that grid point
        // gives the period the flag was read over; it must be >= 2 samples.
        std::size_t flips = 0;
        std::size_t shortPeriodFlips = 0;
        for (const ProfundumCallRecord& r : t.calls) {
            if (r.voice != 0 || r.call != ProfundumCall::HalfWave || !r.issued || r.sample == 0)
                continue;
            ++flips;
            const PT::VoiceEngineChunkRecord* before = nullptr;
            for (const PT::VoiceEngineChunkRecord& c : v.chunks) {
                if (c.sample + c.n <= r.sample)
                    before = &c;
            }
            if (before == nullptr || !(before->meterFreqHz > 0.0f)
                || fs / static_cast<double>(before->meterFreqHz) < 2.0)
                ++shortPeriodFlips;
        }
        std::cout << std::setprecision(6) << "VoiceEngine_MeterBoundedAtTopNote fs " << fs << ": chunks "
                  << v.chunks.size() << ", theta out of range " << thetaOut << ", freq != clamp(f_core) " << freqOff
                  << ", above cap " << aboveCap << ", off cap after 100 ms " << offCapAfterSettle << ", non-finite "
                  << nonFinite << ", HalfWave flips " << flips << " (period < 2 samples: " << shortPeriodFlips
                  << ")\n";
        CHECK_FALSE(v.chunks.empty());
        CHECK(thetaOut == 0);
        CHECK(freqOff == 0);
        CHECK(aboveCap == 0);
        CHECK(offCapAfterSettle == 0);
        CHECK(nonFinite == 0);
        CHECK(shortPeriodFlips == 0);
    }
}
