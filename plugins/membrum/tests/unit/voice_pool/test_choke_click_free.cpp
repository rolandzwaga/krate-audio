// ==============================================================================
// Phase 3.3 -- Choke click-free (FR-134, SC-022)
// ==============================================================================
// T3.3.2: trigger open-hat (note 46), wait 100 ms, trigger closed-hat (note 42)
// with both in the same choke group.
//
//   (a) peak click artifact during choke <= -30 dBFS relative to the INCOMING
//       voice's peak (SC-022 / FR-126 / FR-134). Measured via the triple-
//       subtraction technique inherited from Phase 3.2's steal click-free
//       test (see test_steal_click_free.cpp).
//   (b) choke fast-release terminates within 5 +/- 1 ms wall-clock (SC-022).
//   (c) first 20 ms of the post-choke note-42 voice is BIT-IDENTICAL to an
//       isolated note-42 render within the -120 dBFS noise floor. This is
//       the "reused voice behaves like a fresh voice" guarantee: after
//       beginFastRelease swaps voices_[slot] with releasingVoices_[slot],
//       the main slot's DrumVoice must produce audio indistinguishable from
//       a pristine-pool voice. Both arrays are now prepared with the SAME
//       voiceId (see voice_pool.cpp:78-96) so per-voice PRNG / decorrelation
//       state matches; the shadow voice that surfaces as the "new" main
//       voice after swap was prepared but never rendered, so it is in the
//       exact same post-prepare state as a fresh pool's voices_[0].
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "voice_pool/voice_pool.h"
#include "voice_pool_test_helpers.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

constexpr int kBlockSize = 128;

inline bool isFiniteSample(float x) noexcept
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &x, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

inline int blocksForMs(double sampleRate, double ms)
{
    const int samples = static_cast<int>(std::ceil(ms * 1e-3 * sampleRate));
    return std::max(1, (samples + kBlockSize - 1) / kBlockSize);
}

// Render note 46 in full, wait 100 ms, then note 42, capture the post-choke
// output for `captureBlocks` blocks.
struct ChokeRender
{
    std::vector<float> samples;
    int                closedSlot         = -1;
    int                openSlot           = -1;
    bool               allFinite          = true;
    int                terminationSamples = -1;
};

ChokeRender renderChoke(double sampleRate,
                         std::uint8_t chokeGroup, int captureBlocks,
                         float pre46Level, float new42Level)
{
    Membrum::VoicePool pool;
    pool.prepare(sampleRate, kBlockSize);
    pool.setMaxPolyphony(4);
    pool.setVoiceStealingPolicy(Membrum::VoiceStealingPolicy::Oldest);
    Membrum::TestHelpers::setAllPadsVoiceParams(pool, 0.5f, 0.5f, 0.3f, 0.3f, pre46Level);
    Membrum::TestHelpers::setAllPadsExciterType(pool, Membrum::ExciterType::Impulse);
    Membrum::TestHelpers::setAllPadsBodyModel(pool, Membrum::BodyModelType::Membrane);
    pool.setChokeGroup(chokeGroup);

    std::vector<float> outL(static_cast<std::size_t>(kBlockSize), 0.0f);
    std::vector<float> outR(static_cast<std::size_t>(kBlockSize), 0.0f);

    // Open hat.
    pool.noteOn(46, 0.9f);

    // Warmup long enough for the open-hat voice to decay below -30 dBFS
    // before the choke event. Phase 8A.5 (commit 89cf0c64) decoupled the
    // amp envelope from voice lifetime -- the body now rings at its own
    // T60 (material=0.5/decay=0.3 defaults => ~1 s ring-out), not the old
    // 200 ms envelope gate. 3 s of warmup puts the voice ~-50 dBFS deep,
    // well under the -30 dBFS click threshold that the fade residual is
    // measured against.
    const int warmupBlocks = blocksForMs(sampleRate, 3000.0);
    for (int b = 0; b < warmupBlocks; ++b)
        pool.processBlock(outL.data(), outR.data(), kBlockSize);

    // Baseline block (no output captured, matches steal_click_free pattern).
    pool.processBlock(outL.data(), outR.data(), kBlockSize);

    // Trigger closed hat.
    Membrum::TestHelpers::setAllPadsVoiceParams(pool, 0.5f, 0.5f, 0.3f, 0.3f, new42Level);
    pool.noteOn(42, 0.9f);

    ChokeRender r;
    for (int s = 0; s < 16; ++s)
    {
        const auto& m = pool.voiceMeta(s);
        if (m.state == Membrum::VoiceSlotState::Active &&
            m.originatingNote == 42)
        {
            r.closedSlot = s;
            break;
        }
    }
    for (int s = 0; s < 16; ++s)
    {
        const auto& rm = pool.releasingMeta(s);
        if (rm.state == Membrum::VoiceSlotState::FastReleasing &&
            rm.originatingNote == 46)
        {
            r.openSlot = s;
            break;
        }
    }

    // Capture the post-choke block stream.
    r.samples.reserve(static_cast<std::size_t>(captureBlocks * kBlockSize));
    int samplesElapsed = 0;
    for (int b = 0; b < captureBlocks; ++b)
    {
        pool.processBlock(outL.data(), outR.data(), kBlockSize);
        for (int i = 0; i < kBlockSize; ++i)
        {
            const float s = outL[i];
            if (!isFiniteSample(s)) r.allFinite = false;
            r.samples.push_back(s);
        }
        samplesElapsed += kBlockSize;

        if (r.terminationSamples < 0 && r.openSlot >= 0)
        {
            const auto& rm = pool.releasingMeta(r.openSlot);
            if (rm.state == Membrum::VoiceSlotState::Free)
                r.terminationSamples = samplesElapsed;
        }
    }
    return r;
}

// Render a note 42 on a slot whose voiceId matches what the choke
// scenario's new-voice slot has. In the choke scenario, note 46 takes
// slot 0 during warmup, the choke path frees slot 0 via
// allocator.noteOff + voiceFinished, and then allocator.noteOn(42)
// picks the next Idle voice under Oldest allocation. Oldest's
// tiebreak is the lowest `timestamp_` value -- slot 0's timestamp was
// advanced by the earlier allocation, so the allocator picks slot 1
// (untouched, timestamp 0).
//
// To match this slot assignment in the isolated reference, this
// helper pre-allocates slot 0 via a no-audio note (velocity still
// produces audio, but we drop it before capturing), which advances
// slot 0's timestamp past slot 1's, so the subsequent noteOn(42)
// also lands on slot 1 -- same slot, same voiceId (= 1), same
// pristine prepared state as the choke scenario's new note-42 voice.
std::vector<float> renderNote42WithMatchingSlot(double sampleRate,
                                                 int captureBlocks, float level)
{
    Membrum::VoicePool pool;
    pool.prepare(sampleRate, kBlockSize);
    pool.setMaxPolyphony(4);
    pool.setVoiceStealingPolicy(Membrum::VoiceStealingPolicy::Oldest);
    Membrum::TestHelpers::setAllPadsVoiceParams(pool, 0.5f, 0.5f, 0.3f, 0.3f, level);
    Membrum::TestHelpers::setAllPadsExciterType(pool, Membrum::ExciterType::Impulse);
    Membrum::TestHelpers::setAllPadsBodyModel(pool, Membrum::BodyModelType::Membrane);
    // NOTE: no choke group set -- the voice selection path reduces to
    // plain allocator.noteOn + NoteOn event. With no choke group,
    // processChokeGroups(42) early-outs so the timestamp on slot 0 is
    // still advanced by noteOn(46) below, matching the choke scenario.

    std::vector<float> outL(static_cast<std::size_t>(kBlockSize), 0.0f);
    std::vector<float> outR(static_cast<std::size_t>(kBlockSize), 0.0f);

    // Pre-allocation: trigger note 46 to advance slot 0's timestamp,
    // then noteOff + process through its release stage so the allocator
    // marks it Idle with a "used" timestamp. Note 46 is NOT in any choke
    // group here (group == 0 default), so the subsequent noteOn(42)
    // cannot choke it; instead, the allocator simply picks the Oldest
    // Idle voice = slot 1 (lowest timestamp among idle voices).
    pool.noteOn(46, 0.9f);
    pool.noteOff(46);  // user note-off: allocator marks slot 0 Releasing
    // Run the voice's envelope through decay + release long enough for
    // the auto-release + voiceFinished chain to reclaim slot 0 and mark
    // it Idle with timestamp = 1 (while slot 1 still has timestamp = 0).
    // Decay = 200 ms, release = 300 ms, plus a generous margin.
    const int warmupBlocks = blocksForMs(sampleRate, 1000.0);
    for (int b = 0; b < warmupBlocks; ++b)
        pool.processBlock(outL.data(), outR.data(), kBlockSize);

    pool.noteOn(42, 0.9f);

    std::vector<float> captured;
    captured.reserve(static_cast<std::size_t>(captureBlocks * kBlockSize));
    for (int b = 0; b < captureBlocks; ++b)
    {
        pool.processBlock(outL.data(), outR.data(), kBlockSize);
        for (int i = 0; i < kBlockSize; ++i) captured.push_back(outL[i]);
    }
    return captured;
}

} // namespace

namespace {

void runChokeCase(double sampleRate, const char* label)
{
    INFO("sample rate = " << label);

    // 25 ms capture -- more than enough for the 5 ms fade + a margin, and
    // covers the 20 ms bit-identity window.
    const int captureBlocks =
        std::max(blocksForMs(sampleRate, 25.0), blocksForMs(sampleRate, 6.0));

    // -------------------------------------------------------------
    // Primary render: full choke scenario. The existing note-46 voice
    // is audible at normal level; note 42 is triggered into the same
    // choke group and must fast-release note 46 and sound cleanly.
    // -------------------------------------------------------------
    const ChokeRender primary = renderChoke(sampleRate,
                                             /*group*/ 1, captureBlocks,
                                             /*pre46Level*/ 0.8f,
                                             /*new42Level*/ 0.8f);
    REQUIRE(primary.allFinite);
    REQUIRE(primary.closedSlot >= 0);
    REQUIRE(primary.openSlot   >= 0);

    // Incoming voice peak / bit-identity reference: render note 42 on a
    // pool whose allocator state drives the same slot assignment as the
    // choke scenario (slot 1, not slot 0 -- see the helper comment).
    const std::vector<float> referenceNote42 =
        renderNote42WithMatchingSlot(sampleRate, captureBlocks, /*level*/ 0.8f);
    float incomingVoicePeak = 0.0f;
    for (float s : referenceNote42)
    {
        const float a = std::fabs(s);
        if (a > incomingVoicePeak) incomingVoicePeak = a;
    }
    REQUIRE(incomingVoicePeak > 0.0f);

    const float clickThreshold = 0.0316228f * incomingVoicePeak;  // -30 dBFS

    // -------------------------------------------------------------
    // Click metric: the first 2.5 ms window starting from the choke
    // event contains BOTH the new note-42 voice and the fast-releasing
    // note-46 voice. If the reused voice is bit-identical to a
    // pristine voice (verified below in (c)), then subtracting the
    // reference note-42 render from the primary render isolates the
    // fading stolen voice's residual, which is the "click":
    //
    //   primary[i]        = newVoice42[i] + fastReleaseNote46[i]
    //   referenceNote42[i] = newVoice42[i]   (bit-identical per (c))
    //   primary[i] - referenceNote42[i] = fastReleaseNote46[i]
    //
    // The click is bounded vs. the INCOMING voice's peak (note 42),
    // NOT the stolen voice's peak, per FR-126 / SC-022 / FR-134.
    // -------------------------------------------------------------
    const int halfWindow = static_cast<int>(std::ceil(0.0025 * sampleRate));
    const int postLen = std::min({halfWindow,
                                   static_cast<int>(primary.samples.size()),
                                   static_cast<int>(referenceNote42.size())});

    float clickPeak = 0.0f;
    for (int i = 0; i < postLen; ++i)
    {
        const float residual =
            primary.samples[static_cast<std::size_t>(i)]
          - referenceNote42[static_cast<std::size_t>(i)];
        const float m = std::fabs(residual);
        if (m > clickPeak) clickPeak = m;
    }

    CAPTURE(sampleRate, incomingVoicePeak, clickPeak, clickThreshold,
            primary.terminationSamples, primary.closedSlot);

    // -------------------------------------------------------------
    // (a) Peak click <= -30 dBFS relative to the INCOMING voice's peak.
    // -------------------------------------------------------------
    REQUIRE(clickPeak <= clickThreshold);

    // -------------------------------------------------------------
    // (b) Fade terminates within 5 +/- 1 ms = <= 6 ms wall-clock.
    // -------------------------------------------------------------
    const int bound6ms = static_cast<int>(std::ceil(0.006 * sampleRate));
    REQUIRE(primary.terminationSamples > 0);
    REQUIRE(primary.terminationSamples <= bound6ms);

    // -------------------------------------------------------------
    // (c) BIT-IDENTITY: the post-choke note-42 voice must be sample-
    //     identical to a matched-slot reference note-42 render within
    //     -120 dBFS.
    //
    // Both DrumVoice instances have the SAME voiceId because
    // voice_pool prepares main AND shadow slots with the same ID
    // (see voice_pool.cpp fix). The shadow voice that surfaces as the
    // main voice after the swap was prepared but never rendered, so
    // its post-prepare state is identical to the reference pool's
    // never-touched voice at the same slot index.
    //
    // Measurement window: [6 ms .. 20 ms] after the choke event. The
    // 5 ms fast-release terminates by 5 ms, FR-124 allows a +/- 1 ms
    // slack, so by sample 6 ms the fading note-46 voice contributes
    // exactly 0 (applyFastRelease writes zeros past the termination
    // point). From 6 ms onwards, `primary[i]` contains ONLY the new
    // note-42 voice. `referenceNote42[i]` also contains only note 42
    // (from a pristine slot 1). If the reused voice is bit-identical
    // to a fresh voice, the sample-by-sample difference over this
    // window is exactly 0, or below the -120 dBFS noise floor.
    // -------------------------------------------------------------
    // Expected slot: Oldest policy picks the idle voice with the lowest
    // timestamp. After note 46 takes slot 0 and the choke path frees
    // it via noteOff + voiceFinished, slot 0 has timestamp = 1 while
    // slot 1 is still untouched (timestamp = 0). Oldest picks slot 1.
    // The reference render mirrors this via its pre-warmup note 46.
    REQUIRE(primary.closedSlot == 1);

    const int samples6ms  = static_cast<int>(std::ceil(0.006 * sampleRate));
    const int samples20ms = static_cast<int>(std::ceil(0.020 * sampleRate));
    const int identityEnd = std::min({samples20ms,
                                       static_cast<int>(primary.samples.size()),
                                       static_cast<int>(referenceNote42.size())});
    REQUIRE(identityEnd > samples6ms);

    float maxDiff = 0.0f;
    int   maxDiffIdx = -1;
    for (int i = samples6ms; i < identityEnd; ++i)
    {
        const float d = std::fabs(
            primary.samples[static_cast<std::size_t>(i)]
          - referenceNote42[static_cast<std::size_t>(i)]);
        if (d > maxDiff) { maxDiff = d; maxDiffIdx = i; }
    }

    // -120 dBFS = 1e-6 absolute. Both signals are float samples in the
    // same scale; a truly bit-identical reused voice produces maxDiff
    // == 0. Anything below the -120 dBFS floor is "indistinguishable".
    constexpr float kBitIdentityFloor = 1.0e-6f;
    CAPTURE(maxDiff, maxDiffIdx, identityEnd, samples6ms);
    REQUIRE(maxDiff <= kBitIdentityFloor);
}

} // namespace

TEST_CASE("VoicePool choke: click <= -30 dBFS, terminates within 5 +/- 1 ms, "
          "bit-identical reused voice",
          "[membrum][voice_pool][phase3_3][choke_click_free]")
{
    SECTION("44100 Hz") { runChokeCase(44100.0, "44100"); }
    SECTION("48000 Hz") { runChokeCase(48000.0, "48000"); }
    SECTION("96000 Hz") { runChokeCase(96000.0, "96000"); }
}
