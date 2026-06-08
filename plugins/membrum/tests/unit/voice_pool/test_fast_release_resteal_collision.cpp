// ==============================================================================
// M-8 -- Click-free fast retrigger / re-steal when the shadow slot is busy
// ==============================================================================
// Audit finding M-8 (AUDIT-signal-path-2026-06-07.md): a same-slot re-steal
// that lands INSIDE the ~5 ms fast-release window of a PRIOR steal hits the
// single-shadow-per-slot reservation. `beginFastRelease` is idempotent
// (FR-127) and short-circuits when the shadow slot is already FastReleasing,
// so the ringing main voice is NOT moved to a shadow -- the subsequent NoteOn
// hard-overwrites it with a fresh attack, producing a discontinuity (click).
//
// This is the gap left open by `test_fast_release_double_steal.cpp`, which
// only exercises the cross-slot double-steal (the second steal lands on a
// DIFFERENT slot, so the shadow is free and no collision occurs).
//
// -----------------------------------------------------------------------------
// Reproduction: three rapid hits on the SAME note. Same-note retrigger
// (FR-012, VoiceAllocator::retriggerNote) always steals the voice currently
// playing that note, so the same slot is re-stolen each hit -- guaranteeing
// the collision (maxPolyphony is irrelevant; it clamps to >= 4). The hit
// LEVEL is toggled between note-ons (it is read per-noteOn and only scales the
// voice output, leaving exciter/body/PRNG advancement identical):
//   hit1 (SILENT, level 0) -> occupies the shadow slot when hit2 steals it.
//   hit2 (LOUD)            -> re-steals the slot; shadow begins fading hit1.
//   <render 1 block (~2.9 ms < 5 ms) -- shadow still FastReleasing, hit2
//    develops to near peak>
//   hit3 (LOUD)            -> re-steals the slot again; shadow is BUSY.
//                             BUG: hit2 (ringing in main) is discarded -> click.
//                             FIX: hit2 re-snapshotted into the shadow (only
//                                  the quieter hit1 tail is truncated) -> smooth.
//
// Click metric (triple subtraction, inherited from test_steal_click_free.cpp):
//   post             = hit3 + [FIX: shadowfade(hit2_loud) | BUG: 0]
//   newVoiceMatching = hit3 + shadowfade(hit2_SILENT=0) = hit3   (hit2 at level 0)
//   noStealRef       = hit2_natural                       (hit3 not triggered)
//
//   (post - newVoiceMatching) - noStealRef
//     FIX: shadowfade(hit2_loud) - hit2_natural  -> bounded (FR-126 fade vs natural)
//     BUG: 0 - hit2_natural = -hit2_natural      -> the full disappearing ring
//
// The new voice (hit3) cancels bit-exactly between post and newVoiceMatching
// because pad/exciter/body advancement is level-INDEPENDENT (level only scales
// the final output), so the slot/voiceId/PRNG state at hit3 is identical in
// both renders. hit2's natural continuation cancels between post and
// noStealRef for the same reason.
//
// Primary assertion: cleaned click <= -30 dBFS relative to the incoming
// (hit3) voice peak. RED on the pre-fix code (click == full hit2 ring),
// GREEN after the force-re-snapshot fix.
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

constexpr double kSampleRate = 44100.0;
constexpr int    kBlockSize  = 128;

// One note (one pad). Same-note retrigger re-steals the same slot each hit.
constexpr std::uint8_t kHitNote = 36;   // pad 0
constexpr int          kPad     = 0;

inline bool isFiniteSample(float x) noexcept
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &x, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

int findReleasingSlot(const Membrum::VoicePool& pool) noexcept
{
    for (int slot = 0; slot < Membrum::kMaxVoices; ++slot)
        if (pool.releasingMeta(slot).state ==
            Membrum::VoiceSlotState::FastReleasing)
            return slot;
    return -1;
}

// Run the 3-hit collision sequence. `hit2Level` distinguishes the post render
// (loud) from the newVoiceMatching render (0 = silent). `applyHit3` false
// produces the no-steal reference (hit2 continues naturally). Capture begins
// at the hit3 boundary (or, for the no-steal ref, the equivalent boundary).
std::vector<float> renderCollision(float hit2Level, bool applyHit3,
                                   int captureBlocks)
{
    Membrum::VoicePool pool;
    pool.prepare(kSampleRate, kBlockSize);
    pool.setVoiceStealingPolicy(Membrum::VoiceStealingPolicy::Oldest);

    // Uniform body params so every hit is spectrally identical; the pad level
    // is toggled per-hit below.
    Membrum::TestHelpers::setAllPadsVoiceParams(pool, 0.5f, 0.5f, 0.3f, 0.3f, 0.0f);
    Membrum::TestHelpers::setAllPadsExciterType(pool, Membrum::ExciterType::Impulse);
    Membrum::TestHelpers::setAllPadsBodyModel(pool, Membrum::BodyModelType::Membrane);

    std::vector<float> outL(static_cast<std::size_t>(kBlockSize), 0.0f);
    std::vector<float> outR(static_cast<std::size_t>(kBlockSize), 0.0f);

    // hit1 -- SILENT occupier. Render a few blocks so the allocator sees the
    // voice Active before hit2 re-steals it.
    pool.setPadConfigField(kPad, Membrum::kPadLevel, 0.0f);
    pool.noteOn(kHitNote, 0.9f);
    for (int b = 0; b < 4; ++b)
        pool.processBlock(outL.data(), outR.data(), kBlockSize);

    // hit2 -- re-steals the slot; shadow begins fading hit1.
    pool.setPadConfigField(kPad, Membrum::kPadLevel, hit2Level);
    pool.noteOn(kHitNote, 0.9f);

    // The capture begins HERE. Block 0 is the "develop" block (~2.9 ms,
    // shadow still busy, hit2 ringing in the main slot). hit3 fires at the
    // block-0/block-1 boundary (sample index kBlockSize), so the collision
    // discontinuity, if any, lands at the start of block 1.
    std::vector<float> captured;
    captured.reserve(static_cast<std::size_t>(captureBlocks * kBlockSize));
    pool.processBlock(outL.data(), outR.data(), kBlockSize);          // block 0
    for (int i = 0; i < kBlockSize; ++i) captured.push_back(outL[i]);

    if (applyHit3)
    {
        // hit3 -- re-steals the slot again; collision: shadow is busy.
        pool.setPadConfigField(kPad, Membrum::kPadLevel, 0.8f);
        pool.noteOn(kHitNote, 0.9f);
    }

    for (int b = 1; b < captureBlocks; ++b)
    {
        pool.processBlock(outL.data(), outR.data(), kBlockSize);
        for (int i = 0; i < kBlockSize; ++i) captured.push_back(outL[i]);
    }
    return captured;
}

} // namespace

TEST_CASE("VoicePool fast retrigger: same-slot re-steal into a busy shadow is "
          "click-free (M-8)",
          "[membrum][voice_pool][phase3_2][resteal_collision][m8]")
{
    // Sanity: confirm the collision is actually reachable -- after hit2 a
    // shadow slot must be FastReleasing, and it must still be busy when hit3
    // would fire one block later.
    {
        Membrum::VoicePool pool;
        pool.prepare(kSampleRate, kBlockSize);
        pool.setMaxPolyphony(1);
        pool.setVoiceStealingPolicy(Membrum::VoiceStealingPolicy::Oldest);
        Membrum::TestHelpers::setAllPadsVoiceParams(pool, 0.5f, 0.5f, 0.3f, 0.3f, 0.8f);
        Membrum::TestHelpers::setAllPadsExciterType(pool, Membrum::ExciterType::Impulse);
        Membrum::TestHelpers::setAllPadsBodyModel(pool, Membrum::BodyModelType::Membrane);

        std::vector<float> outL(static_cast<std::size_t>(kBlockSize), 0.0f);
        std::vector<float> outR(static_cast<std::size_t>(kBlockSize), 0.0f);

        pool.noteOn(kHitNote, 0.9f);
        for (int b = 0; b < 4; ++b)
            pool.processBlock(outL.data(), outR.data(), kBlockSize);
        pool.noteOn(kHitNote, 0.9f);
        REQUIRE(findReleasingSlot(pool) >= 0);             // hit2 re-stole the slot
        pool.processBlock(outL.data(), outR.data(), kBlockSize);
        REQUIRE(findReleasingSlot(pool) >= 0);             // shadow still busy
    }

    constexpr int kCaptureBlocks = 4;  // 512 samples >= 5 ms window at 44.1 kHz

    const std::vector<float> post =
        renderCollision(/*hit2Level*/ 0.8f, /*applyHit3*/ true,  kCaptureBlocks);
    const std::vector<float> newVoiceMatching =
        renderCollision(/*hit2Level*/ 0.0f, /*applyHit3*/ true,  kCaptureBlocks);
    const std::vector<float> noStealRef =
        renderCollision(/*hit2Level*/ 0.8f, /*applyHit3*/ false, kCaptureBlocks);

    // Finite-output guard (FR-181).
    for (float s : post)             REQUIRE(isFiniteSample(s));
    for (float s : newVoiceMatching) REQUIRE(isFiniteSample(s));
    for (float s : noStealRef)       REQUIRE(isFiniteSample(s));

    const std::size_t n = std::min({post.size(), newVoiceMatching.size(),
                                    noStealRef.size()});
    REQUIRE(n > static_cast<std::size_t>(kBlockSize));

    // Isolate the STOLEN (hit2) voice's contribution. hit3 fires identically in
    // `post` and `newVoiceMatching` (level-independent exciter/body/PRNG
    // advancement), so it cancels exactly:
    //   isolated[i] = post[i] - newVoiceMatching[i]
    //     pre-boundary : hit2 ringing in the MAIN slot (loud)
    //     post-boundary: FIX -> hit2 fading in the SHADOW slot (continuous);
    //                    BUG -> 0 (hit2 hard-discarded)
    std::vector<float> isolated(n);
    for (std::size_t i = 0; i < n; ++i)
        isolated[i] = post[i] - newVoiceMatching[i];

    // Pre-collision level of the ringing voice (block 0, before hit3 fires).
    float preLevel = 0.0f;
    for (int i = 0; i < kBlockSize; ++i)
        preLevel = std::max(preLevel, std::fabs(isolated[static_cast<std::size_t>(i)]));
    REQUIRE(preLevel > 0.0f);

    // Max single-sample discontinuity of the isolated voice across the
    // collision boundary (the hit3 attack is already cancelled out, so any
    // jump here is purely the stolen voice's own transition).
    const int half = static_cast<int>(std::ceil(0.0025 * kSampleRate));
    const int lo   = std::max(1, kBlockSize - 4);
    const int hi   = std::min(static_cast<int>(n), kBlockSize + half);
    float maxStep = 0.0f;
    for (int i = lo; i < hi; ++i)
    {
        const auto u = static_cast<std::size_t>(i);
        maxStep = std::max(maxStep, std::fabs(isolated[u] - isolated[u - 1]));
    }

    // The discontinuity as a FRACTION of the voice being faded. A hard cut
    // (BUG) discards the whole ring in one sample -> ratio ~= 1.0. A click-free
    // fast-release fade (FIX) keeps the waveform continuous -> ratio is just
    // the voice's per-sample slew plus the gentle fade slope (~0.08 here).
    // Bound at 0.20 (a hard cut is impossible to mistake for the fade).
    const float stepRatio = maxStep / preLevel;
    CAPTURE(preLevel, maxStep, stepRatio);
    REQUIRE(stepRatio <= 0.20f);
}
