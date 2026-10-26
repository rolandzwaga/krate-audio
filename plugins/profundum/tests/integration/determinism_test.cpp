#include <catch2/catch_test_macros.hpp>
#include "profundum_test_fixture.h"

// Profundum_Determinism — written by T025 (specs/profundum-phase11-parameters/tasks.md)
//
// FR-063 / SC-022: two processors given the same state bytes and the same event / parameter stream render the same
// output, per-sample max |diff| == 0, in the same binary and process (no golden is compared across binaries).
//
// Drift arm (FR-043 reset point): Motion Source 900 = Drift, Target 901 = Mass, Amount 902 = +1, Depth 903 = 1,
// E1 MONO, 4 s. Every arm instance has the same history; then the state stream is reloaded and setActive(false) /
// setActive(true) is called. With the seed changed in the stream, the frame-wise band-level difference
// (band_level_metric.h) against the base over [0.5 s, 4 s) is >= 0.5 dB in at least one frame and band; with the seed
// unchanged it is exactly 0.0. The changed-seed arm is the null arm's control (non-vacuity).

#include "band_level_metric.h"
#include "parameters/sound_param_row.h"
#include "parameters/sound_param_table.h"
#include "plugin_ids.h"

#include <krate/dsp/systems/bass_morph_engine.h>
#include <krate/dsp/systems/morph_state.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

using namespace ProfundumPluginTest;

namespace {

using ScriptedEvent = ProcessorFixture::ScriptedEvent;
using Endpoint = Krate::DSP::BassMorphEngine::Endpoint;
using MotionConfig = Krate::DSP::MorphState::MotionConfig;

constexpr double kFs = 48000.0;
constexpr std::size_t kBlock = 512;
constexpr std::size_t kNullSamples = 96000;    // 2 s
constexpr std::size_t kDriftSamples = 192000;  // 4 s
constexpr std::size_t kDriftFrom = 24000;      // 0.5 s
constexpr std::size_t kHistorySamples = 12000; // 250 ms rendered by every drift-arm instance before the reload
constexpr double kMinDriftDiffDb = 0.5;
constexpr std::uint32_t kChangedSeed = 0x0BADC0DEu;
constexpr std::uint8_t kLeafMotionTarget = 37;
static_assert(std::string_view(::Profundum::lawOf(kLeafMotionTarget).name) == "motion.target");

/// The normalised value of the plain value @p plain of sound parameter @p id (its leaf's law; list rows: index).
[[nodiscard]] double normOf(Steinberg::Vst::ParamID id, double plain) {
    const ::Profundum::SoundParamRow* row = ::Profundum::soundRowOf(id);
    REQUIRE(row != nullptr);
    return ::Profundum::normalizedFromPlain(::Profundum::lawOf(row->leaf), plain);
}

/// A v2 state with Motion on (Drift -> Mass, Amount +1, Depth 1) and voice mode MONO, written through the
/// parameter path (Edit Target A, position 0) of a prepared processor and read back by getState.
[[nodiscard]] StateBytes driftMotionState() {
    using namespace ::Profundum;
    constexpr double kDriftIndex = static_cast<double>(static_cast<int>(MotionConfig::Source::Drift));
    constexpr double kMassIndex = static_cast<double>(static_cast<int>(MotionConfig::Target::Mass));
    ProcessorFixture fx;
    REQUIRE(fx.prepare(kFs) == Steinberg::kResultOk);
    REQUIRE(fx.setParamsInBlock({
                {kVoiceModeId, 0.0},  // Mono
                {kMotionSourceId, normOf(kMotionSourceId, kDriftIndex)},
                {kMotionTargetId, normOf(kMotionTargetId, kMassIndex)},
                {kMotionAmountId, normOf(kMotionAmountId, 1.0)},
                {kMotionDepthId, normOf(kMotionDepthId, 1.0)},
            }) == Steinberg::kResultOk);
    StateBytes s = writeV2Stream(*fx.proc);
    REQUIRE(s.size() == kStateV3Bytes);
    return s;
}

/// Motion is on in endpoint A of @p fx (non-vacuity: the stream carried the Drift settings).
void requireDriftMotion(const ProcessorFixture& fx) {
    REQUIRE(fx.proc->endpointSlotForTest(Endpoint::A, kLeafMotionSource) ==
            static_cast<std::uint32_t>(MotionConfig::Source::Drift));
    REQUIRE(fx.proc->endpointSlotForTest(Endpoint::A, kLeafMotionTarget) ==
            static_cast<std::uint32_t>(MotionConfig::Target::Mass));
}

/// E1 note-on at 0, held for the whole render. Returns the capture index at which the stream starts.
[[nodiscard]] std::size_t renderHeldE1(ProcessorFixture& fx, std::size_t samples) {
    ScriptedEvent on{};
    on.sample = 0;
    on.kind = ScriptedEvent::Kind::NoteOn;
    on.pitch = kE1;
    on.velocity = 1.0f;
    const std::array<ScriptedEvent, 1> stream{on};
    const std::size_t at = fx.capturedL.size();
    fx.renderScript(std::span<const ScriptedEvent>(stream), samples, kBlock);
    REQUIRE(fx.capturedL.size() == at + samples);
    return at;
}

/// One drift-arm instance: @p state loaded in the host order, the shared history rendered, then @p reload set
/// through setState and the FR-043 reset point (setActive false / true), then E1 held 4 s. Returns the capture
/// index of the E1 stream.
[[nodiscard]] std::size_t renderDriftArm(ProcessorFixture& fx, const StateBytes& state, const StateBytes& reload) {
    REQUIRE(loadStateBytes(*fx.proc, state) == Steinberg::kResultOk);
    REQUIRE(fx.prepare(kFs) == Steinberg::kResultOk);
    requireDriftMotion(fx);
    fx.renderScript(std::span<const ScriptedEvent>{}, kHistorySamples, kBlock);
    REQUIRE(loadStateBytes(*fx.proc, reload) == Steinberg::kResultOk);
    REQUIRE(fx.proc->setActive(0u) == Steinberg::kResultOk);
    REQUIRE(fx.proc->setActive(1u) == Steinberg::kResultOk);
    requireDriftMotion(fx);
    return renderHeldE1(fx, kDriftSamples);
}

/// [at + kDriftFrom, at + kDriftSamples) of one channel's capture.
[[nodiscard]] std::span<const float> driftWindowOf(const std::vector<float>& captured, std::size_t at) {
    return std::span<const float>(captured).subspan(at + kDriftFrom, kDriftSamples - kDriftFrom);
}

}  // namespace

TEST_CASE("Profundum_Determinism", "[profundum][determinism]") {
    const StateBytes state = driftMotionState();

    SECTION("IdenticalStateAndStreamRenderIdentically") {
        ProcessorFixture p1;
        ProcessorFixture p2;
        for (ProcessorFixture* fx : {&p1, &p2}) {
            REQUIRE(loadStateBytes(*fx->proc, state) == Steinberg::kResultOk);
            REQUIRE(fx->prepare(kFs) == Steinberg::kResultOk);
            requireDriftMotion(*fx);
        }
        const std::size_t at1 = renderHeldE1(p1, kNullSamples);
        const std::size_t at2 = renderHeldE1(p2, kNullSamples);
        REQUIRE(at1 == 0u);
        REQUIRE(at2 == 0u);

        // Non-vacuity: the render sounds.
        const double rms = rmsDb(p1.capturedL);
        CAPTURE(rms);
        CHECK(rms > -90.0);

        const float diffL = maxAbsDiff(p1.capturedL, p2.capturedL);
        const float diffR = maxAbsDiff(p1.capturedR, p2.capturedR);
        CAPTURE(diffL, diffR);
        CHECK(diffL == 0.0f);
        CHECK(diffR == 0.0f);
        CHECK(mismatches(p1.capturedL, p2.capturedL) == 0u);
        CHECK(mismatches(p1.capturedR, p2.capturedR) == 0u);
    }

    SECTION("SeedResetChangesDriftRender") {
        StateBytes changed = state;
        REQUIRE(readU32(state, kV2SeedAt) != kChangedSeed);
        patchU32(changed, kV2SeedAt, kChangedSeed);

        ProcessorFixture base;
        ProcessorFixture changedSeed;
        ProcessorFixture sameSeed;
        const std::size_t baseAt = renderDriftArm(base, state, state);
        const std::size_t changedAt = renderDriftArm(changedSeed, state, changed);
        const std::size_t sameAt = renderDriftArm(sameSeed, state, state);

        CHECK(rmsDb(driftWindowOf(base.capturedL, baseAt)) > -90.0);  // non-vacuity: the window sounds

        const double changedDb = bandLevelDifferenceDb(
            driftWindowOf(base.capturedL, baseAt), driftWindowOf(base.capturedR, baseAt),
            driftWindowOf(changedSeed.capturedL, changedAt), driftWindowOf(changedSeed.capturedR, changedAt), kFs,
            /*frameWise=*/true);
        const double sameDb = bandLevelDifferenceDb(
            driftWindowOf(base.capturedL, baseAt), driftWindowOf(base.capturedR, baseAt),
            driftWindowOf(sameSeed.capturedL, sameAt), driftWindowOf(sameSeed.capturedR, sameAt), kFs,
            /*frameWise=*/true);
        CAPTURE(changedDb, sameDb);
        WARN("SC-022 drift arm (frame-wise, [0.5 s, 4 s)): changed seed " << changedDb << " dB (bar >= "
                                                                          << kMinDriftDiffDb << "), same seed "
                                                                          << sameDb << " dB (bar == 0)");
        CHECK(changedDb >= kMinDriftDiffDb);
        CHECK(sameDb == 0.0);
    }
}
