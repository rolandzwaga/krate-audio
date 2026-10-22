#include <catch2/catch_test_macros.hpp>
#include "profundum_test_fixture.h"

// Profundum_MonoSumViaParams — written by T024 (specs/profundum-phase11-parameters/tasks.md)
//
// FR-065 / SC-019 (plan 6.2): the Phase 9 full-engine mono-sum check (SC-015 / SC-016,
// dsp/tests/unit/systems/profundum_engine_chain_test.cpp:160-385) re-run through the host parameter surface.
//   Grid: Image Mode SOLID / SPREAD / WIDE x MONO / 4 voices, E1, A / B = randomValidMorphState(0x119A / 0x119B).
//   ID 800 (and Spread 801 = 1, the Phase 9 cell rule at :161-162) is written under Edit Target A and again under
//   Edit Target B, so both endpoints carry the mode. Morph Position 105 = 0.5 is reached by the engine's ramp from
//   0, so the selections stay A (kThetaDown / kThetaUp, bass_morph_engine.h:70-71).
//   SOLID cells start in WIDE and switch both endpoints to SOLID at ~0.5 s (Phase 9 :162-163): B first, then A, so
//   the block that writes A (the selection the resolver reads) is the block that sets the mode.
// Assertions:
//   - StereoFieldTest::bandReading(L, R, n, fs, 0, f_c) on the field-stage and engine output taps over the Phase 9
//     read window [2 s, 2 s + 2^17), gated as in Phase 9 (r.valid and E1 below f_c): |Q| <= kSubBandQBarDb (1.0 dB)
//     and rho >= kSubBandRhoBar (0.99);
//   - SOLID: the processor output is L == R bitwise from (start of the block that set the mode
//     + ceil(kModeCrossfadeMs * fs / 1000) + kLimiterLookaheadSamples) on;
//   - every voice's anchorL == anchorR bitwise in every chunk, in every mode and both voice counts;
//   - non-vacuity arm: in SPREAD with Spread 801 = 1, the output L != R somewhere (so the L == R checks can fail).
// [long]: run by the main loop at close (T031).

#include "bass_stereo_field_test_helpers.h"  // read-only Phase 9 helper (plan 6.4; tests/CMakeLists.txt include dir)

#include <krate/dsp/systems/bass_morph_engine.h>
#include <krate/dsp/systems/bass_stereo_field.h>
#include <krate/dsp/systems/morph_state.h>
#include <krate/dsp/systems/profundum_engine.h>
#include <krate/dsp/systems/profundum_engine_observer.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <vector>

using namespace ProfundumPluginTest;

namespace {

namespace mspSft = Krate::DSP::StereoFieldTest;
using MspImg = Krate::DSP::BassStereoField::ImageMode;
using MspEngine = Krate::DSP::ProfundumEngine;
using Krate::DSP::detail::morph_state_io::leafBitsEqual;

constexpr double kMspFs = 48000.0;
constexpr std::size_t kMspFsInt = 48000;
constexpr std::size_t kMspBlock = 512;
constexpr std::uint32_t kMspSeedA = 0x119Au;  // plan 6.1
constexpr std::uint32_t kMspSeedB = 0x119Bu;
constexpr std::size_t kMspSettle = 2u * kMspFsInt;        // Phase 9 kPchMsSettleS = 2 s (:170)
constexpr std::size_t kMspWindow = std::size_t{1} << 17;  // Phase 9 kPchMsWindow (:171)
constexpr std::size_t kMspTotal = ((kMspSettle + kMspWindow + kMspBlock - 1u) / kMspBlock + 1u) * kMspBlock;
static_assert(kMspSettle + kMspWindow <= kMspTotal);
// The SOLID switch: the first block boundary at or after 0.5 s (Phase 9 kPchMsSwitchS, :172). The B write is in the
// block before it; the A write (the mode-setting block) starts at kMspSwitch.
constexpr std::size_t kMspSwitch = ((kMspFsInt / 2u + kMspBlock - 1u) / kMspBlock) * kMspBlock;
static_assert(kMspSwitch % kMspBlock == 0u && kMspSwitch >= kMspBlock);
// Phase 9 4-voice offsets (profundum_engine_chain_test.cpp:173): root + {0, 5, 7, 10} st.
constexpr std::array<int, 4> kMspPolyOffsetsSt{0, 5, 7, 10};

// ceil(kModeCrossfadeMs * fs / 1000) in integer arithmetic (no /fp:fast rounding at the ceil).
static_assert(Krate::DSP::BassStereoField::kModeCrossfadeMs == 10.0f, "bass_stereo_field.h:91");
constexpr std::size_t kMspCrossfadeSamples = (10u * kMspFsInt + 999u) / 1000u;  // 480 at 48 kHz
constexpr std::size_t kMspSolidFrom = kMspSwitch + kMspCrossfadeSamples + MspEngine::kLimiterLookaheadSamples;
static_assert(kMspSolidFrom < kMspSettle, "the crossfade has ended before the read window");

/// The Image Mode list (field_params.h:38-39): Solid 0, Spread 1, Wide 2 over three entries.
[[nodiscard]] double mspModeNorm(MspImg m) noexcept { return static_cast<double>(static_cast<int>(m)) / 2.0; }

[[nodiscard]] const char* mspModeName(MspImg m) noexcept {
    switch (m) {
        case MspImg::Solid:
            return "SOLID";
        case MspImg::Spread:
            return "SPREAD";
        case MspImg::Wide:
            return "WIDE";
    }
    return "?";
}

/// The FR-067 tap seam's consumer: the engine chunk taps appended in order, and the per-voice Anchor identity
/// counted per chunk. Never asserts inside a callback (they are noexcept); the case reads the record afterwards.
class MspObserver final : public Krate::DSP::ProfundumEngineObserver {
public:
    std::vector<float> fieldL, fieldR, outL, outR;
    std::size_t voiceChunks = 0;
    std::size_t anchorMismatchChunks = 0;
    std::uint64_t firstAnchorMismatchSample = 0;
    double anchorPeak = 0.0;
    std::array<std::size_t, MspEngine::kMaxVoices> chunksPerVoice{};
    // The resolved state (onGrid).
    bool haveGrid = false;
    MspImg lastMode = MspImg::Spread;
    float lastCrossoverHz = 0.0f;
    float lastPosition = 0.0f;
    float maxPosition = 0.0f;
    bool sawSolid = false;
    std::uint64_t firstSolidGridSample = 0;
    std::size_t dcResyncs = 0;
    std::uint64_t firstDcResyncSample = 0;

    explicit MspObserver(std::size_t total) {
        for (std::vector<float>* v : {&fieldL, &fieldR, &outL, &outR}) {
            v->resize(total);   // written in place by append(); finish() trims to what was written
        }
    }

    /// After the render, off the audio thread: each tap holds exactly the samples written.
    void finish() {
        fieldL.resize(used_[0]);
        fieldR.resize(used_[1]);
        outL.resize(used_[2]);
        outR.resize(used_[3]);
    }

    void onGrid(const Krate::DSP::ProfundumGridRecord& r) noexcept override {
        if (r.resolved == nullptr) {
            return;
        }
        haveGrid = true;
        lastMode = r.resolved->state.field.mode;
        lastCrossoverHz = r.resolved->state.field.crossoverHz;
        lastPosition = r.resolved->position;
        maxPosition = std::max(maxPosition, r.resolved->position);
        if (lastMode == MspImg::Solid && !sawSolid) {
            sawSolid = true;
            firstSolidGridSample = r.sample;
        }
    }

    void onCall(const Krate::DSP::ProfundumCallRecord& r) noexcept override {
        if (r.call == Krate::DSP::ProfundumCall::DcResync) {
            if (dcResyncs == 0) {
                firstDcResyncSample = r.sample;
            }
            ++dcResyncs;
        }
    }

    void onVoiceChunk(const Krate::DSP::ProfundumVoiceTaps& t) noexcept override {
        ++voiceChunks;
        if (t.voice < chunksPerVoice.size()) {
            ++chunksPerVoice[t.voice];
        }
        const bool sizesOk = t.anchorL.size() >= t.n && t.anchorR.size() >= t.n;
        const bool equal =
            sizesOk && (t.n == 0 || std::memcmp(t.anchorL.data(), t.anchorR.data(), t.n * sizeof(float)) == 0);
        if (!equal) {
            if (anchorMismatchChunks == 0) {
                firstAnchorMismatchSample = t.sample;
            }
            ++anchorMismatchChunks;
        }
        if (sizesOk) {
            for (std::size_t i = 0; i < t.n; ++i) {
                anchorPeak = std::max(anchorPeak, std::abs(static_cast<double>(t.anchorL[i])));
            }
        }
    }

    void onEngineChunk(const Krate::DSP::ProfundumEngineTaps& t) noexcept override {
        append(fieldL, used_[0], t.fieldL, t.n);
        append(fieldR, used_[1], t.fieldR, t.n);
        append(outL, used_[2], t.outL, t.n);
        append(outR, used_[3], t.outR, t.n);
    }

private:
    // Never grows on the audio thread: a tap that would overflow is dropped, and the size REQUIRE then fails.
    static void append(std::vector<float>& dst, std::size_t& used, std::span<const float> src, std::size_t n) noexcept {
        const std::size_t m = std::min(n, src.size());
        if (used + m <= dst.size()) {
            std::copy_n(src.begin(), m, dst.begin() + static_cast<std::ptrdiff_t>(used));
            used += m;
        }
    }

    std::array<std::size_t, 4> used_{};   // fieldL, fieldR, outL, outR
};

struct MspCell {
    MspImg mode = MspImg::Solid;
    bool poly = false;
};

[[nodiscard]] std::string mspLabel(const MspCell& c) {
    return std::string(mspModeName(c.mode)) + (c.poly ? ",POLY4" : ",MONO");
}

/// The count of bitwise L != R samples over [from, min(to, size)).
[[nodiscard]] std::size_t mspCountDiffering(const std::vector<float>& L, const std::vector<float>& R,
                                            std::size_t from, std::size_t to) {
    std::size_t n = 0;
    for (std::size_t i = from; i < to && i < L.size() && i < R.size(); ++i) {
        if (std::bit_cast<std::uint32_t>(L[i]) != std::bit_cast<std::uint32_t>(R[i])) {
            ++n;
        }
    }
    return n;
}

void mspRunCell(const MspCell& c) {
    const std::string label = mspLabel(c);
    INFO("cell " << label);

    auto fx = std::make_unique<ProcessorFixture>();

    // 1. Distinct A / B endpoints through the host surface (a v2 setState stream; plan 6.1 seeds).
    const Krate::DSP::MorphState a = randomValidMorphState(kMspSeedA);
    const Krate::DSP::MorphState b = randomValidMorphState(kMspSeedB);
    REQUIRE(loadStateBytes(*fx->proc, withEndpoints(writeV2Stream(*fx->proc), a, b)) == Steinberg::kResultOk);

    // 2. Voice mode / polyphony, then the mode under test (SOLID cells start in WIDE) and Spread 801 = 1, written
    //    under Edit Target A and again under Edit Target B (pass 1 reads the Edit Target first, processor.cpp:669).
    const MspImg firstMode = c.mode == MspImg::Solid ? MspImg::Wide : c.mode;
    MultiParamChanges pc;
    pc.reserve(4);
    pc.clear();
    pc.addQueue(::Profundum::kVoiceModeId).addTestPoint(0, c.poly ? 1.0 : 0.0);
    pc.addQueue(::Profundum::kPolyphonyId).addTestPoint(0, 3.0 / 7.0);  // "4" (global_params.h:85-86)
    REQUIRE(fx->processNoOutputs(&pc) == Steinberg::kResultOk);
    for (const double target : {0.0, 1.0}) {
        pc.clear();
        pc.addQueue(::Profundum::kMorphEditTargetId).addTestPoint(0, target);
        pc.addQueue(::Profundum::kFieldImageModeId).addTestPoint(0, mspModeNorm(firstMode));
        pc.addQueue(::Profundum::kFieldSpreadId).addTestPoint(0, 1.0);
        REQUIRE(fx->processNoOutputs(&pc) == Steinberg::kResultOk);
    }

    // 3. The tap seam (FR-067), attached before setupProcessing.
    MspObserver obs(kMspTotal);
    fx->proc->setEngineObserverForTest(&obs);
    REQUIRE(fx->prepare(kMspFs, static_cast<Steinberg::int32>(kMspBlock)) == Steinberg::kResultOk);

    // 4. The script: note(s) on at 0 with Morph Position 0.5 (ramped from 0 by the engine); SOLID cells write
    //    SOLID under B in the block before kMspSwitch and under A in the block at kMspSwitch.
    using Ev = ProcessorFixture::ScriptedEvent;
    std::vector<Ev> script;
    script.push_back(Ev{.sample = 0, .kind = Ev::Kind::Param, .id = ::Profundum::kMorphPositionId, .value = 0.5});
    const int root = static_cast<int>(kE1);
    if (c.poly) {
        for (const int off : kMspPolyOffsetsSt) {
            script.push_back(Ev{.sample = 0,
                                .kind = Ev::Kind::NoteOn,
                                .pitch = static_cast<Steinberg::int16>(root + off),
                                .velocity = 100.0f / 127.0f});
        }
    } else {
        script.push_back(Ev{.sample = 0, .kind = Ev::Kind::NoteOn, .pitch = kE1, .velocity = 100.0f / 127.0f});
    }
    if (c.mode == MspImg::Solid) {
        const auto bAt = static_cast<Steinberg::int64>(kMspSwitch - kMspBlock);
        const auto aAt = static_cast<Steinberg::int64>(kMspSwitch);
        script.push_back(
            Ev{.sample = bAt, .kind = Ev::Kind::Param, .id = ::Profundum::kMorphEditTargetId, .value = 1.0});
        script.push_back(Ev{.sample = bAt,
                            .kind = Ev::Kind::Param,
                            .id = ::Profundum::kFieldImageModeId,
                            .value = mspModeNorm(MspImg::Solid)});
        script.push_back(
            Ev{.sample = aAt, .kind = Ev::Kind::Param, .id = ::Profundum::kMorphEditTargetId, .value = 0.0});
        script.push_back(Ev{.sample = aAt,
                            .kind = Ev::Kind::Param,
                            .id = ::Profundum::kFieldImageModeId,
                            .value = mspModeNorm(MspImg::Solid)});
    }
    fx->renderScript(script, kMspTotal, kMspBlock);

    // 5. Detach while inactive (FR-067), before the observer goes out of scope.
    REQUIRE(fx->proc->endpointRejectionsForTest() == 0u);
    REQUIRE(fx->engine() != nullptr);
    REQUIRE(fx->engine()->droppedEvents() == 0u);
    fx->proc->setActive(0u);
    fx->proc->setEngineObserverForTest(nullptr);
    obs.finish();

    const std::vector<float>& hostL = fx->capturedL;
    const std::vector<float>& hostR = fx->capturedR;
    REQUIRE(hostL.size() == kMspTotal);
    REQUIRE(hostR.size() == kMspTotal);
    for (const std::vector<float>* v : {&obs.fieldL, &obs.fieldR, &obs.outL, &obs.outR}) {
        REQUIRE(v->size() == kMspTotal);
    }

    // The mode reached the engine through ID 800, and the selections stayed A (p never reached kThetaUp).
    REQUIRE(obs.haveGrid);
    REQUIRE(obs.lastMode == c.mode);
    INFO("resolved position " << obs.lastPosition << ", max " << obs.maxPosition << " (kThetaUp "
                              << Krate::DSP::BassMorphEngine::kThetaUp << ")");
    CHECK(std::abs(obs.lastPosition - 0.5f) <= 1e-3f);  // the ramp has landed (2 s >> the 40 ms default ramp)
    CHECK(obs.maxPosition < Krate::DSP::BassMorphEngine::kThetaUp);

    // SC-016 through the host surface: every voice's Anchor tap bitwise L == R in every chunk.
    INFO("voice chunks " << obs.voiceChunks << ", Anchor L != R chunks " << obs.anchorMismatchChunks
                         << " (first at sample " << obs.firstAnchorMismatchSample << "), Anchor peak "
                         << obs.anchorPeak);
    REQUIRE(obs.voiceChunks > 0u);
    if (c.poly) {
        std::size_t voicesSeen = 0;
        for (const std::size_t n : obs.chunksPerVoice) {
            voicesSeen += n > 0u ? 1u : 0u;
        }
        CHECK(voicesSeen >= kMspPolyOffsetsSt.size());
    }
    CHECK(obs.anchorPeak > 0.0);  // the identity is not read on silence
    CHECK(obs.anchorMismatchChunks == 0u);

    // SOLID: the output L == R bitwise from the mode-setting block start + crossfade + limiter latency.
    if (c.mode == MspImg::Solid) {
        const std::size_t differing = mspCountDiffering(hostL, hostR, kMspSolidFrom, kMspTotal);
        INFO("first SOLID grid sample " << obs.firstSolidGridSample << ", DcResync records " << obs.dcResyncs
                                        << " (first at " << obs.firstDcResyncSample << "), switch block "
                                        << kMspSwitch << ", identity from " << kMspSolidFrom
                                        << "; host L != R samples from there: " << differing
                                        << ", before the switch: " << mspCountDiffering(hostL, hostR, 0, kMspSwitch));
        REQUIRE(obs.sawSolid);
        CHECK(obs.firstSolidGridSample >= kMspSwitch);  // the cell started in WIDE
        CHECK(differing == 0u);
    }

    // Non-vacuity arm: SPREAD at Spread 1 leaves L != R somewhere, so the L == R checks above can fail.
    if (c.mode == MspImg::Spread) {
        const std::size_t differing = mspCountDiffering(hostL, hostR, kMspSettle, kMspTotal);
        INFO("SPREAD host output L != R samples in the read window: " << differing);
        CHECK(differing > 0u);
    }

    // SC-015 through the host surface: the sub band [0, f_c) on the field and output taps (Phase 9 :323-358).
    const double fc = static_cast<double>(obs.lastCrossoverHz);
    const double lowestHz = 440.0 * std::pow(2.0, (static_cast<double>(root) - 69.0) / 12.0);
    const bool subInBand = lowestHz < fc;  // Phase 7 G10-2
    struct Tap {
        const char* name;
        const std::vector<float>* L;
        const std::vector<float>* R;
    };
    const std::array<Tap, 2> taps{{{.name = "field", .L = &obs.fieldL, .R = &obs.fieldR}, {.name = "out", .L = &obs.outL, .R = &obs.outR}}};
    for (const Tap& tap : taps) {
        const mspSft::BandReading r = mspSft::bandReading(tap.L->data() + kMspSettle, tap.R->data() + kMspSettle,
                                                          kMspWindow, kMspFs, 0.0, fc);
        const bool gated = subInBand && r.valid;
        std::cout << "SC-019," << label << ',' << tap.name << ",fc_hz," << fc << ",sub_in_band,"
                  << (subInBand ? 1 : 0) << ",valid," << (r.valid ? 1 : 0) << ",q_sub_db," << r.q << ",rho_sub,"
                  << r.rho << ",s2m_db," << r.sideToMidDb << ",gated," << (gated ? 1 : 0) << '\n';
        if (gated) {
            INFO(tap.name << " Q_sub " << r.q << " dB (bar |Q| <= " << mspSft::kSubBandQBarDb << "), rho " << r.rho
                          << " (bar >= " << mspSft::kSubBandRhoBar << "), f_c " << fc << " Hz");
            CHECK(std::abs(r.q) <= mspSft::kSubBandQBarDb);
            CHECK(r.rho >= mspSft::kSubBandRhoBar);
        }
    }
}

}  // namespace

TEST_CASE("Profundum_MonoSumViaParams", "[profundum][mono][long]") {
    REQUIRE_FALSE(leafBitsEqual(randomValidMorphState(kMspSeedA), randomValidMorphState(kMspSeedB)));
    for (const MspImg mode : {MspImg::Solid, MspImg::Spread, MspImg::Wide}) {
        for (const bool poly : {false, true}) {
            mspRunCell(MspCell{.mode = mode, .poly = poly});
        }
    }
}
