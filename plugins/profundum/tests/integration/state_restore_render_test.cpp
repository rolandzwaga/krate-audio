#include <catch2/catch_test_macros.hpp>
#include "profundum_test_fixture.h"

// Profundum_StateRestoreRender — written by T025 (specs/profundum-phase11-parameters/tasks.md)
//
// SC-011 (FR-041, FR-042, FR-043): a project saved with no voice sounding and restored in the host order
// (setState BEFORE setupProcessing / setActive(true)) renders a new note like the original's continued render.
// The fixture is pinned so that the morph runtime blob is the only state that can differ: the seeded-random
// endpoints have motion.source in {Off, Env} and field.motionDepth = 0 (Motion Drift / Cycle phase and field
// drift are not in the 510-byte state), and the original runs under the loaded seed (setActive false / true after
// its setState, FR-043) as the restored instance does.
//
// Setup as SC-010 (state_v2_test.cpp Profundum_StateRoundTripV2): random A / B, priorFieldsP's non-default note
// block, position 1 -> 0.5 at a 40 ms ramp (every selection B, kept inside the hysteresis band), Edit Target B,
// seed 0x1234, 2 s rendered. As in SC-010 no note is played before the save, so every note is released; the
// getTailSamples() of silence then elapses before getState.
//
// Measure: band-level difference (band_level_metric.h, aggregate mode) over [100 ms, 2.1 s) after the first new
// note-on <= 0.1 dB; the selection-mask byte (18) and the Anchor byte (19) of engineForTest()->serializeMorphRuntime
// equal. Control arm (teeth): the stream's blob version byte (490) forced to 0xFF -> the blob is rejected (FR-042
// step 3 skipped) and the restored mask byte reads 0x00 against the original's 0x0F.
//
// [long]: deferred to the main loop at close (T031).

#include "band_level_metric.h"
#include "parameters/morph_params.h"
#include "plugin_ids.h"

#include <krate/dsp/systems/bass_morph_engine.h>
#include <krate/dsp/systems/morph_state.h>
#include <krate/dsp/systems/profundum_engine.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

using namespace ProfundumPluginTest;

namespace {

using RuntimeBlob = std::array<std::byte, Krate::DSP::BassMorphEngine::kMorphRuntimeBytes>;
using ScriptedEvent = ProcessorFixture::ScriptedEvent;

constexpr double kFs = 48000.0;
constexpr std::size_t kBlock = 512;
constexpr std::size_t kHoldSamples = 96000;     // the new note is held 2 s
constexpr std::size_t kStreamSamples = 100800;  // 2.1 s: the window's end
constexpr std::size_t kWindowFrom = 4800;       // 100 ms after the first new note-on
constexpr double kMaxBandDiffDb = 0.1;
constexpr std::uint8_t kAllB = 0x0F;
constexpr std::uint8_t kAllA = 0x00;

/// The engine's own runtime blob right now (serializeMorphRuntime, profundum_engine.h).
[[nodiscard]] RuntimeBlob engineRuntimeOf(const ProcessorFixture& fx) {
    REQUIRE(fx.engine() != nullptr);
    RuntimeBlob b{};
    REQUIRE(fx.engine()->serializeMorphRuntime(b.data(), b.size()) == b.size());
    return b;
}

[[nodiscard]] std::uint8_t byteAt(const RuntimeBlob& b, std::size_t at) {
    return std::to_integer<std::uint8_t>(b[at]);
}

/// The identical post-save stream: E1 note-on at 0, note-off at 2 s (MONO is the state's voice mode). Renders 2.1 s
/// and returns the capture index at which the stream starts.
[[nodiscard]] std::size_t renderNewNoteStream(ProcessorFixture& fx) {
    ScriptedEvent on{};
    on.sample = 0;
    on.kind = ScriptedEvent::Kind::NoteOn;
    on.pitch = kE1;
    on.velocity = 1.0f;
    ScriptedEvent off{};
    off.sample = static_cast<Steinberg::int64>(kHoldSamples);
    off.kind = ScriptedEvent::Kind::NoteOff;
    off.pitch = kE1;
    const std::array<ScriptedEvent, 2> stream{on, off};
    const std::size_t at = fx.capturedL.size();
    fx.renderScript(std::span<const ScriptedEvent>(stream), kStreamSamples, kBlock);
    REQUIRE(fx.capturedL.size() == at + kStreamSamples);
    return at;
}

/// [at + kWindowFrom, at + kStreamSamples) of one channel's capture.
[[nodiscard]] std::span<const float> windowOf(const std::vector<float>& captured, std::size_t at) {
    return std::span<const float>(captured).subspan(at + kWindowFrom, kStreamSamples - kWindowFrom);
}

}  // namespace

TEST_CASE("Profundum_StateRestoreRender", "[profundum][state][long]") {
    using namespace ::Profundum;

    const RandomStateConstraints pinned{.motionSourceOffOrEnv = true, .fieldMotionDepthZero = true};
    const Krate::DSP::MorphState a = randomValidMorphState(0x11Cu, pinned);
    const Krate::DSP::MorphState b = randomValidMorphState(0x11Du, pinned);

    // ---- The original: SC-010 setup, then the tail of silence, then the save. ----
    ProcessorFixture src;
    REQUIRE(src.prepare(kFs) == Steinberg::kResultOk);
    V2Fields f = priorFieldsP();
    f.position = 1.0f;
    f.rampMs = 40.0f;
    f.anchorMorphMode = kDefaultAnchorMorphModeIndex;
    f.editTarget = 1;  // B
    f.seed = 0x1234u;
    REQUIRE(f.globals.voiceMode == 0);  // MONO
    REQUIRE(loadStateBytes(*src.proc, withFields(withEndpoints(writeV2Stream(*src.proc), a, b), f)) ==
            Steinberg::kResultOk);
    // FR-043: the loaded seed takes effect at the reset point, so the original renders under 0x1234 like the
    // restored instance (whose setState precedes setupProcessing).
    REQUIRE(src.proc->setActive(0u) == Steinberg::kResultOk);
    REQUIRE(src.proc->setActive(1u) == Steinberg::kResultOk);

    ScriptedEvent toHalf{};
    toHalf.sample = 12000;  // 250 ms: p has settled at 1 (every selection B by rule 1)
    toHalf.kind = ScriptedEvent::Kind::Param;
    toHalf.id = kMorphPositionId;
    toHalf.value = 0.5;
    src.renderScript(std::span<const ScriptedEvent>(&toHalf, 1), 96000u, kBlock);

    // Save point: no voice sounds. No note was played, and the FR-060 tail elapses before getState.
    const std::uint32_t tail = src.proc->getTailSamples();
    REQUIRE(tail > 0u);
    src.renderScript(std::span<const ScriptedEvent>{}, static_cast<std::size_t>(tail), kBlock);
    {
        const std::size_t n = src.capturedL.size();
        REQUIRE(n >= kBlock);
        CHECK(peakOf(std::span<const float>(src.capturedL).subspan(n - kBlock)) == 0.0f);
        CHECK(peakOf(std::span<const float>(src.capturedR).subspan(n - kBlock)) == 0.0f);
    }

    const StateBytes saved = writeV2Stream(*src.proc);
    REQUIRE(saved.size() == kStateV3Bytes);
    // Precondition (teeth of the control arm): the original's selections are all B.
    REQUIRE(byteAt(engineRuntimeOf(src), kRtMainMaskOffset) == kAllB);
    REQUIRE(std::to_integer<std::uint8_t>(saved[kStateRuntimeOffset + kRtMainMaskOffset]) == kAllB);

    // ---- The restored instance: setState first, then setupProcessing / setActive(true) (host order). ----
    ProcessorFixture dst;
    REQUIRE(loadStateBytes(*dst.proc, saved) == Steinberg::kResultOk);
    REQUIRE(dst.prepare(kFs) == Steinberg::kResultOk);

    // ---- Control arm: the blob's version byte (stream byte 490) forced to 0xFF, so FR-042 step 3 is skipped. ----
    StateBytes ignored = saved;
    ignored[kStateRuntimeOffset + kRtVersionOffset] = std::byte{0xFF};
    ProcessorFixture ctl;
    REQUIRE(loadStateBytes(*ctl.proc, ignored) == Steinberg::kResultOk);
    REQUIRE(ctl.prepare(kFs) == Steinberg::kResultOk);

    // ---- The identical new-note stream into the original (continued), the restored and the control. ----
    const std::size_t srcAt = renderNewNoteStream(src);
    const std::size_t dstAt = renderNewNoteStream(dst);
    static_cast<void>(renderNewNoteStream(ctl));

    CHECK(dst.proc->restoreApplyCountForTest() == 1u);  // the blob was accepted and applied once
    CHECK(ctl.proc->restoreApplyCountForTest() == 0u);  // the 0xFF blob was rejected

    // Non-vacuity: the window sounds in both renders.
    const double srcRms = rmsDb(windowOf(src.capturedL, srcAt));
    const double dstRms = rmsDb(windowOf(dst.capturedL, dstAt));
    CAPTURE(srcRms, dstRms);
    CHECK(srcRms > -90.0);
    CHECK(dstRms > -90.0);

    const double diffDb = bandLevelDifferenceDb(windowOf(src.capturedL, srcAt), windowOf(src.capturedR, srcAt),
                                                windowOf(dst.capturedL, dstAt), windowOf(dst.capturedR, dstAt), kFs,
                                                /*frameWise=*/false);
    CAPTURE(diffDb);
    WARN("SC-011 band-level difference (aggregate, [100 ms, 2.1 s)): " << diffDb << " dB (bar " << kMaxBandDiffDb
                                                                        << " dB)");
    CHECK(diffDb <= kMaxBandDiffDb);

    // The selected Character algorithm / image mode (main mask, byte 18) and the Anchor selection (byte 19) read
    // back identical through the runtime blob.
    const RuntimeBlob srcRt = engineRuntimeOf(src);
    const RuntimeBlob dstRt = engineRuntimeOf(dst);
    const RuntimeBlob ctlRt = engineRuntimeOf(ctl);
    const int srcMask = byteAt(srcRt, kRtMainMaskOffset);
    const int dstMask = byteAt(dstRt, kRtMainMaskOffset);
    const int ctlMask = byteAt(ctlRt, kRtMainMaskOffset);
    const int srcAnchor = byteAt(srcRt, kRtAnchorSelectionOffset);
    const int dstAnchor = byteAt(dstRt, kRtAnchorSelectionOffset);
    CAPTURE(srcMask, dstMask, ctlMask, srcAnchor, dstAnchor);
    CHECK(dstMask == srcMask);
    CHECK(dstAnchor == srcAnchor);
    CHECK(std::cmp_equal(srcMask, kAllB));

    // Control arm (teeth): the ignored blob leaves the restored selections all A against the original's all B.
    CHECK(std::cmp_equal(ctlMask, kAllA));
    CHECK(ctlMask != srcMask);
}

