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

#include <krate/dsp/core/db_utils.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

constexpr int kBlockSize = 128;

// NaN/Inf check that survives -ffast-math: the shared detail::isFinite reads the bits through db_utils.h's
// barrier. A local memcpy + mask (this helper's old body) is folded to "finite" by newer Apple clang.
inline bool isFiniteSample(float x) noexcept
{
    return ::Krate::DSP::detail::isFinite(x);
}

inline int blocksForMs(double sampleRate, double ms, int blockSize = kBlockSize)
{
    const int samples = static_cast<int>(std::ceil(ms * 1e-3 * sampleRate));
    return std::max(1, (samples + blockSize - 1) / blockSize);
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

    // Warmup long enough for the open-hat voice to decay well into its tail
    // before the choke event. Phase 8A.5 (commit 89cf0c64) decoupled the
    // amp envelope from voice lifetime -- the body now rings at its own
    // T60 (material=0.5/decay=0.3 defaults => ~1 s ring-out), not the old
    // 200 ms envelope gate. Audit N-1 replaced the impulse-bound body
    // normalisation with a measured-strike one (~6 dBFS strike-peak budget),
    // which removes the clipping that previously pinned hits near 0 dBFS and so
    // drops the absolute tail level further -- voice 46 now crosses the
    // -60 dBFS silence threshold sooner. 1 s of warmup keeps voice 46 audibly
    // Active (above -60 dBFS) at the choke while still leaving a low-level tail
    // for the click-free fade assertion.
    const int warmupBlocks = blocksForMs(sampleRate, 1000.0);
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
    // Render until the auto-release + voiceFinished chain has reclaimed
    // slot 0 and marked it Idle with timestamp = 1 (while slot 1 still has
    // timestamp = 0). The note-off does not shorten the hit (FR-114, fix plan
    // T2.9), so this waits for the natural decay (about 1.7 s here); 20 s is
    // the retirement guarantee of test_voice_retirement.cpp.
    const int warmupCap = blocksForMs(sampleRate, 20000.0);
    int warmupBlocks = 0;
    while (pool.isAnyVoiceActive() && warmupBlocks < warmupCap)
    {
        pool.processBlock(outL.data(), outR.data(), kBlockSize);
        ++warmupBlocks;
    }
    REQUIRE_FALSE(pool.isAnyVoiceActive());

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

// ==============================================================================
// T2.6 (MB-148) -- releasing a slot whose shadow is still fading
// ==============================================================================
// A steal leaves the slot's shadow fading for 5 ms. A choke, a Quietest
// pre-steal or a polyphony shrink that releases the same slot inside that
// window used to find the shadow busy: the non-forced hand-over returned
// early, the slot was marked Free anyway, and the sounding voice was either
// hard-overwritten by the next NoteOn on that slot (a click) or left ringing
// unramped.
//
// Each SECTION renders the open hat (note 46, pad 10, level > 0, long decay)
// twice: once released while its slot's shadow is busy (fading a silent seed),
// once released the same way with a free shadow. In both the hat plays on a
// never-used DrumVoice prepared with the same voiceId at the same age, so the
// free-shadow render is the hat's normal 5 ms fast release. The click is the
// difference between the two: the busy shadow may only truncate its own
// (silent) tail, so the hat must fade exactly as it does with a free shadow.
//
//   click     = max |hatBusy - hatFree|          <= -30 dB of the hat's
//                                                   pre-release peak
//   residual  = max |hatBusy| from 6 ms onwards  <= -60 dB of that peak
// ==============================================================================

namespace {

constexpr double kBusySampleRate = 48000.0;
constexpr int    kBusyBlock      = 32;   // 0.67 ms: events land within 2 ms
constexpr std::uint8_t kOpenHat  = 46;   // pad 10
constexpr int    kOpenHatPad     = 10;

enum class BusyRelease { Choke, QuietestPreSteal, Shrink };

struct BusyRender
{
    std::vector<float> out;            // main L, from the first block on
    int                releaseSample = -1;
    int                hatSlot       = -1;
    bool               shadowBusy    = false;
    bool               allFinite     = true;
};

void configureBusyPool(Membrum::VoicePool& pool, BusyRelease kind, float hatLevel)
{
    pool.prepare(kBusySampleRate, kBusyBlock);
    pool.setMaxPolyphony(kind == BusyRelease::Shrink ? 8 : 4);
    pool.setVoiceStealingPolicy(kind == BusyRelease::QuietestPreSteal
                                    ? Membrum::VoiceStealingPolicy::Quietest
                                    : Membrum::VoiceStealingPolicy::Oldest);
    Membrum::TestHelpers::setAllPadsVoiceParams(pool, 0.5f, 0.5f, 0.3f, 0.3f, 0.0f);
    Membrum::TestHelpers::setAllPadsExciterType(pool, Membrum::ExciterType::Impulse);
    Membrum::TestHelpers::setAllPadsBodyModel(pool, Membrum::BodyModelType::Membrane);
    pool.setPadConfigField(kOpenHatPad, Membrum::kPadDecay, 1.0f);
    pool.setPadConfigField(kOpenHatPad, Membrum::kPadLevel, hatLevel);
    if (kind == BusyRelease::Choke)
    {
        // Only the choke case groups the hats: in the shrink case note 42 is
        // one of the seeds, and the open hat must take its slot by a steal.
        pool.setPadChokeGroup(kOpenHatPad, 1);  // open hat (46)
        pool.setPadChokeGroup(6, 1);            // closed hat (42), level 0
    }
    if (kind == BusyRelease::QuietestPreSteal)
    {
        // Three audible seeds, so the hat is the quietest voice when the
        // pre-steal picks its victim.
        for (int p = 0; p < 3; ++p)
            pool.setPadConfigField(p, Membrum::kPadLevel, 0.8f);
    }
}

int slotPlaying(const Membrum::VoicePool& pool, std::uint8_t note)
{
    for (int s = 0; s < Membrum::kMaxVoices; ++s)
    {
        const auto& m = pool.voiceMeta(s);
        if (m.state == Membrum::VoiceSlotState::Active && m.originatingNote == note)
            return s;
    }
    return -1;
}

BusyRender renderBusyRelease(BusyRelease kind, bool busy, float hatLevel)
{
    Membrum::VoicePool pool;
    configureBusyPool(pool, kind, hatLevel);

    BusyRender r;
    std::vector<float> outL(static_cast<std::size_t>(kBusyBlock), 0.0f);
    std::vector<float> outR(static_cast<std::size_t>(kBusyBlock), 0.0f);
    auto render = [&](int blocks) {
        for (int b = 0; b < blocks; ++b)
        {
            pool.processBlock(outL.data(), outR.data(), kBusyBlock);
            for (int i = 0; i < kBusyBlock; ++i)
            {
                const float s = outL[static_cast<std::size_t>(i)];
                if (!Krate::DSP::detail::isFinite(s))
                    r.allFinite = false;
                r.out.push_back(s);
            }
        }
    };

    // Block 0: the seeds that fill the pool (silent, except the Quietest
    // seeds). The free-shadow render leaves out the seed the hat displaces.
    switch (kind)
    {
    case BusyRelease::Choke:
        if (busy)
            for (int n = 36; n <= 39; ++n)
                pool.noteOn(static_cast<std::uint8_t>(n), 0.9f);
        break;
    case BusyRelease::QuietestPreSteal:
        for (int n = 36; n <= (busy ? 39 : 38); ++n)
            pool.noteOn(static_cast<std::uint8_t>(n), 0.9f);
        break;
    case BusyRelease::Shrink:
        for (int n = 36; n <= (busy ? 43 : 39); ++n)
            pool.noteOn(static_cast<std::uint8_t>(n), 0.9f);
        break;
    }
    render(kind == BusyRelease::QuietestPreSteal ? 2 : 1);

    if (kind == BusyRelease::Shrink && busy)
    {
        // Retrigger the seeds of slots 0..3 so slot 4 holds the oldest voice.
        for (int n = 36; n <= 39; ++n)
            pool.noteOn(static_cast<std::uint8_t>(n), 0.9f);
    }

    // The hat: busy -> it takes its slot by a steal (Oldest) or a pre-steal
    // (Quietest) of a silent seed, whose shadow is now fading; free -> it
    // takes an idle slot.
    pool.noteOn(kOpenHat, 0.9f);
    r.hatSlot = slotPlaying(pool, kOpenHat);
    if (r.hatSlot >= 0)
        r.shadowBusy = pool.releasingMeta(r.hatSlot).state ==
                       Membrum::VoiceSlotState::FastReleasing;

    // Release within 2 ms of the hat (two 0.67 ms blocks).
    render(2);
    r.releaseSample = static_cast<int>(r.out.size());
    switch (kind)
    {
    case BusyRelease::Choke:            pool.noteOn(42, 0.9f); break;  // pad 6, level 0
    case BusyRelease::QuietestPreSteal: pool.noteOn(41, 0.9f); break;  // pad 5, level 0
    case BusyRelease::Shrink:           pool.setMaxPolyphony(4); break;
    }

    render(blocksForMs(kBusySampleRate, 20.0, kBusyBlock));
    return r;
}

// The hat's own output: the render itself when every other voice is silent,
// else the render minus the same render with the hat's level at 0 (the level
// only scales the voice output, so everything else cancels).
std::vector<float> hatOnly(BusyRelease kind, bool busy, const BusyRender& withHat)
{
    if (kind != BusyRelease::QuietestPreSteal)
        return withHat.out;
    const BusyRender noHat = renderBusyRelease(kind, busy, 0.0f);
    REQUIRE(noHat.out.size() == withHat.out.size());
    std::vector<float> hat(withHat.out.size());
    for (std::size_t i = 0; i < hat.size(); ++i)
        hat[i] = withHat.out[i] - noHat.out[i];
    return hat;
}

void runBusyReleaseCase(BusyRelease kind)
{
    const float hatLevel = (kind == BusyRelease::QuietestPreSteal) ? 0.1f : 1.0f;

    BusyRender busy    = renderBusyRelease(kind, /*busy*/ true, hatLevel);
    BusyRender freeRef = renderBusyRelease(kind, /*busy*/ false, hatLevel);
    REQUIRE(busy.allFinite);
    REQUIRE(freeRef.allFinite);

    // The scenario really meets a busy shadow, and the reference does not.
    REQUIRE(busy.shadowBusy);
    REQUIRE_FALSE(freeRef.shadowBusy);
    REQUIRE(busy.hatSlot >= 0);
    REQUIRE(busy.hatSlot == freeRef.hatSlot);
    if (kind == BusyRelease::Shrink)
        REQUIRE(busy.hatSlot >= 4);
    REQUIRE(busy.releaseSample == freeRef.releaseSample);
    REQUIRE(busy.out.size() == freeRef.out.size());

    const std::vector<float> hatBusy = hatOnly(kind, true, busy);
    const std::vector<float> hatFree = hatOnly(kind, false, freeRef);

    float preReleasePeak = 0.0f;
    for (int i = 0; i < freeRef.releaseSample; ++i)
        preReleasePeak = std::max(preReleasePeak,
                                  std::fabs(hatFree[static_cast<std::size_t>(i)]));
    REQUIRE(preReleasePeak > 0.0f);

    float click = 0.0f;
    for (std::size_t i = 0; i < hatBusy.size(); ++i)
        click = std::max(click, std::fabs(hatBusy[i] - hatFree[i]));

    const int from6ms =
        busy.releaseSample + static_cast<int>(std::ceil(0.006 * kBusySampleRate));
    float residual = 0.0f;
    for (std::size_t i = static_cast<std::size_t>(from6ms); i < hatBusy.size(); ++i)
        residual = std::max(residual, std::fabs(hatBusy[i]));

    const float clickBound    = 0.0316228f * preReleasePeak;  // -30 dB
    const float residualBound = 0.001f * preReleasePeak;      // -60 dB
    CAPTURE(busy.hatSlot, preReleasePeak, click, clickBound, residual, residualBound);
    CHECK(residual <= residualBound);
    CHECK(click <= clickBound);
}

} // namespace

TEST_CASE("Choke into a slot whose shadow is still fading mutes the main voice within 6 ms",
          "[voice_pool][choke][m8]")
{
    SECTION("choke into a busy shadow") { runBusyReleaseCase(BusyRelease::Choke); }
    SECTION("Quietest pre-steal into a busy shadow")
    {
        runBusyReleaseCase(BusyRelease::QuietestPreSteal);
    }
    SECTION("polyphony shrink into a busy shadow") { runBusyReleaseCase(BusyRelease::Shrink); }
}

// ==============================================================================
// T2.6 (MB-148) -- three same-slot steals inside one fast-release window
// ==============================================================================
// A same-note retrigger steals the note's own slot. Hits 1, 2 and 3 land
// 1.33 ms apart, so hit 3 re-steals the slot while hit 1 is still fading in
// the shadow (the M-8 collision, measured as in
// test_fast_release_resteal_collision.cpp: a hit is isolated by rendering the
// sequence again with that hit's level at 0, the level only scaling output).
// Hit 3 hands hit 2 to the shadow and truncates hit 1's tail, which has faded
// for one block by then: that truncation is the click, bounded at -30 dB of
// the incoming hit's peak. Hit 2 must cross the hand-over continuously (the
// resteal file's 0.20 step ratio), and no ghost voice may remain.
// ==============================================================================

namespace {

constexpr int kTripleBlock = 64;  // 1.33 ms at 48 kHz

std::vector<float> renderTripleSteal(const float (&levels)[3], int captureBlocks,
                                     int* ghostsAfter = nullptr)
{
    Membrum::VoicePool pool;
    pool.prepare(kBusySampleRate, kTripleBlock);
    pool.setVoiceStealingPolicy(Membrum::VoiceStealingPolicy::Oldest);
    Membrum::TestHelpers::setAllPadsVoiceParams(pool, 0.5f, 0.5f, 0.3f, 0.3f, 0.0f);
    Membrum::TestHelpers::setAllPadsExciterType(pool, Membrum::ExciterType::Impulse);
    Membrum::TestHelpers::setAllPadsBodyModel(pool, Membrum::BodyModelType::Membrane);

    std::vector<float> outL(static_cast<std::size_t>(kTripleBlock), 0.0f);
    std::vector<float> outR(static_cast<std::size_t>(kTripleBlock), 0.0f);
    std::vector<float> captured;
    for (int b = 0; b < captureBlocks; ++b)
    {
        if (b < 3)
        {
            pool.setPadConfigField(0, Membrum::kPadLevel, levels[b]);
            pool.noteOn(36, 0.9f);
        }
        pool.processBlock(outL.data(), outR.data(), kTripleBlock);
        captured.insert(captured.end(), outL.begin(), outL.end());
    }
    if (ghostsAfter != nullptr)
    {
        for (int b = 0; b < blocksForMs(kBusySampleRate, 100.0, kTripleBlock); ++b)
            pool.processBlock(outL.data(), outR.data(), kTripleBlock);
        *ghostsAfter = Membrum::TestHelpers::countGhosts(pool);
    }
    return captured;
}

} // namespace

TEST_CASE("Three same-slot steals within 5 ms are click-free", "[voice_pool][steal][m8]")
{
    constexpr float kL = 0.8f;
    const int captureBlocks = blocksForMs(kBusySampleRate, 10.0, kTripleBlock);

    int ghosts = -1;
    const float all[3]   = {kL, kL, kL};
    const float no1[3]   = {0.0f, kL, kL};
    const float no2[3]   = {kL, 0.0f, kL};
    const float no3[3]   = {kL, kL, 0.0f};
    const std::vector<float> post   = renderTripleSteal(all, captureBlocks, &ghosts);
    const std::vector<float> noHit1 = renderTripleSteal(no1, captureBlocks);
    const std::vector<float> noHit2 = renderTripleSteal(no2, captureBlocks);
    const std::vector<float> noHit3 = renderTripleSteal(no3, captureBlocks);

    const std::size_t n = post.size();
    REQUIRE(n == noHit1.size());
    REQUIRE(n == noHit2.size());
    REQUIRE(n == noHit3.size());
    for (float s : post)
        REQUIRE(Krate::DSP::detail::isFinite(s));

    std::vector<float> hit1(n);
    std::vector<float> hit2(n);
    float incomingPeak = 0.0f;
    for (std::size_t i = 0; i < n; ++i)
    {
        hit1[i] = post[i] - noHit1[i];
        hit2[i] = post[i] - noHit2[i];
        incomingPeak = std::max(incomingPeak, std::fabs(post[i] - noHit3[i]));
    }
    REQUIRE(incomingPeak > 0.0f);

    // Window around hit 3 (sample 2 * kTripleBlock): 4 samples before, 2.5 ms after.
    const int b3   = 2 * kTripleBlock;
    const int lo   = b3 - 4;
    const int hi   = std::min(static_cast<int>(n),
                              b3 + static_cast<int>(std::ceil(0.0025 * kBusySampleRate)));
    float hit1Step = 0.0f;
    float hit2Step = 0.0f;
    for (int i = lo; i < hi; ++i)
    {
        const auto u = static_cast<std::size_t>(i);
        hit1Step = std::max(hit1Step, std::fabs(hit1[u] - hit1[u - 1]));
        hit2Step = std::max(hit2Step, std::fabs(hit2[u] - hit2[u - 1]));
    }
    float hit2PreLevel = 0.0f;
    for (int i = kTripleBlock; i < b3; ++i)
        hit2PreLevel = std::max(hit2PreLevel, std::fabs(hit2[static_cast<std::size_t>(i)]));
    REQUIRE(hit2PreLevel > 0.0f);

    const float clickBound = 0.0316228f * incomingPeak;  // -30 dB
    const float hit2Ratio  = hit2Step / hit2PreLevel;
    CAPTURE(incomingPeak, hit1Step, clickBound, hit2Step, hit2PreLevel, hit2Ratio, ghosts);
    CHECK(hit1Step <= clickBound);
    CHECK(hit2Ratio <= 0.20f);
    CHECK(ghosts == 0);
}
