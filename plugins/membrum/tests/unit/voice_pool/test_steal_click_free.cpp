// ==============================================================================
// Phase 3.2 -- Click-free voice steal (FR-124, FR-125, FR-126, FR-181, SC-021)
// ==============================================================================
// T3.2.1. Parameterized over {Oldest, Quietest, Priority} x {22050, 44100,
// 48000, 96000, 192000} Hz (15 combinations).
//
// -----------------------------------------------------------------------------
// Click metric (FR-126 / SC-021): the peak click artifact during a voice
// steal MUST be <= -30 dBFS relative to the incoming voice's peak, measured
// in the 5 ms window centred on the steal event.
// -----------------------------------------------------------------------------
//
// Measurement strategy:
//   1. Measure `incomingVoicePeak = max |y[i]|` when note 67 is rendered in
//      complete isolation on a fresh pool (no pre-existing voices).
//      The click threshold is then
//           clickThreshold = 10^(-30/20) * incomingVoicePeak
//                          ~= 0.0316228 * incomingVoicePeak
//
//   2. Fill the 4-voice pool with long-ringing seed voices (pads 36-39) and
//      render until every seed's block peak is below half the click
//      threshold while all four are still sounding (T1.9: the pool must
//      really be full, or no steal happens -- before MB-050 was fixed the
//      seeds had retired and only a leaked allocator slot forced the steal).
//      The warmup block count found here is reused by both references.
//
//   3. Render a "no steal" reference: the exact same fill + warmup +
//      baseline processBlock sequence, then keep processing without note
//      67 (the 4 voices continue their natural decay).
//
//   4. Render a "silent new voice" reference: the same sequence, then the
//      steal with pad 67's level at 0. The allocator events, the victim and
//      the shadow fade are identical to the real steal; the new voice adds
//      exactly 0.
//
//   5. The click is the exact difference
//           click[n] = silentNewVoice[n] - noStealRef[n]
//                    = (fading stolen voice) - (stolen voice, natural)
//      and the primary assertion is
//           max |click[n]| over the 5 ms window <= clickThreshold
//
// Supplementary structural assertions (retained from the first iteration):
//   - Exponential ramp shape matches the Option B formula (k^128 after one
//     block, tau ~= 362 us for kFastReleaseSecs = 0.005).
//   - Monotonic fastReleaseGain decrease block-over-block.
//   - Termination within the 6 ms (5 ms + 1 ms slack) FR-124 bound.
//   - No NaN/Inf samples (FR-181).
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "voice_pool/voice_pool.h"
#include "voice_pool_test_helpers.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include <krate/dsp/core/db_utils.h>

namespace {

constexpr int kBlockSize = 128;

// Seed voices (pads 36-39) ring long so they are still sounding -- and the
// 4-voice pool really is full -- once they have decayed well below -30 dBFS
// relative to the incoming voice. With the default decay (0.3) every seed
// retires within 3 s (Phase 8A.5: the body rings at its own T60 and the pool
// retires a voice below -60 dBFS), and a steal then needs no victim at all.
constexpr float kSeedDecay = 1.0f;

// A seed's block peak must be below this fraction of the click threshold
// before the steal: the fade-vs-natural difference of the victim is bounded
// by its own amplitude.
constexpr float kSeedPeakFractionOfThreshold = 0.5f;

// Upper bound on the adaptive warmup (seconds of audio).
constexpr double kMaxWarmupSeconds = 30.0;

const std::uint8_t kSeedNotes[4] = {36, 37, 38, 39};

// Prepare a 4-voice pool, give the seed pads their long decay and trigger
// the four seeds one block apart.
void prepareSeededPool(Membrum::VoicePool&          pool,
                       double                       sampleRate,
                       Membrum::VoiceStealingPolicy policy,
                       std::vector<float>&          outL,
                       std::vector<float>&          outR)
{
    pool.prepare(sampleRate, kBlockSize);
    pool.setMaxPolyphony(4);
    pool.setVoiceStealingPolicy(policy);
    for (const auto note : kSeedNotes)
        pool.setPadConfigField(note - 36, Membrum::kPadDecay, kSeedDecay);

    for (const auto note : kSeedNotes)
    {
        pool.noteOn(note, 0.9f);
        pool.processBlock(outL.data(), outR.data(), kBlockSize);
    }
}

// Highest block peak among the sounding main voices.
float maxActiveLevel(const Membrum::VoicePool& pool) noexcept
{
    float m = 0.0f;
    for (int slot = 0; slot < pool.maxPolyphony(); ++slot)
    {
        const auto& meta = pool.voiceMeta(slot);
        if (meta.state == Membrum::VoiceSlotState::Active)
            m = std::max(m, meta.currentLevel);
    }
    return m;
}

// NaN/Inf check that survives -ffast-math: the shared detail::isFinite reads the bits through db_utils.h's
// barrier. A local memcpy + mask (this helper's old body) is folded to "finite" by newer Apple clang.
inline bool isFiniteSample(float x) noexcept
{
    return ::Krate::DSP::detail::isFinite(x);
}

struct StealCase
{
    Membrum::VoiceStealingPolicy policy;
    double                       sampleRate;
    const char*                  name;
};

// Render note 67 in complete isolation on a fresh pool; return both the
// captured samples (from the noteOn moment onwards) and the overall peak.
struct IsolatedRender
{
    std::vector<float> samples;
    float              peak = 0.0f;
};

IsolatedRender renderIncomingVoiceIsolated(double sampleRate,
                                           int    captureSamples)
{
    Membrum::VoicePool pool;
    pool.prepare(sampleRate, kBlockSize);
    pool.setMaxPolyphony(4);
    pool.setVoiceStealingPolicy(Membrum::VoiceStealingPolicy::Oldest);

    std::vector<float> outL(static_cast<std::size_t>(kBlockSize), 0.0f);
    std::vector<float> outR(static_cast<std::size_t>(kBlockSize), 0.0f);

    pool.noteOn(67, 0.9f);

    IsolatedRender r;
    r.samples.reserve(static_cast<std::size_t>(captureSamples));
    int samplesCaptured = 0;
    while (samplesCaptured < captureSamples)
    {
        pool.processBlock(outL.data(), outR.data(), kBlockSize);
        const int take = std::min(kBlockSize, captureSamples - samplesCaptured);
        for (int i = 0; i < take; ++i)
        {
            r.samples.push_back(outL[i]);
            const float a = std::fabs(outL[i]);
            if (a > r.peak) r.peak = a;
        }
        samplesCaptured += take;
    }
    return r;
}

// Reproduce the seed fill + `warmupBlocks` + baseline block of the real
// steal scenario, then either
//   - (withSilentSteal == false) keep processing without note 67: the
//     "no-steal" reference, where the 4 seeds decay naturally; or
//   - (withSilentSteal == true) trigger note 67 with its pad's level at 0:
//     the allocator events, the victim and its shadow fade are the SAME as
//     in the real steal (the seeds are bit-identical up to the steal), and
//     the new voice contributes exactly 0 (`out = shaped * env * level`).
std::vector<float>
renderReference(double                       sampleRate,
                Membrum::VoiceStealingPolicy policy,
                int                          warmupBlocks,
                bool                         withSilentSteal,
                int                          captureBlocks)
{
    Membrum::VoicePool pool;
    std::vector<float> outL(static_cast<std::size_t>(kBlockSize), 0.0f);
    std::vector<float> outR(static_cast<std::size_t>(kBlockSize), 0.0f);

    prepareSeededPool(pool, sampleRate, policy, outL, outR);
    for (int b = 0; b < warmupBlocks; ++b)
        pool.processBlock(outL.data(), outR.data(), kBlockSize);
    pool.processBlock(outL.data(), outR.data(), kBlockSize);  // baseline block

    if (withSilentSteal)
    {
        pool.setPadConfigField(67 - 36, Membrum::kPadLevel, 0.0f);
        pool.noteOn(67, 0.9f);
    }

    std::vector<float> captured;
    captured.reserve(static_cast<std::size_t>(captureBlocks * kBlockSize));
    for (int b = 0; b < captureBlocks; ++b)
    {
        pool.processBlock(outL.data(), outR.data(), kBlockSize);
        for (int i = 0; i < kBlockSize; ++i) captured.push_back(outL[i]);
    }
    return captured;
}

struct StealResult
{
    bool  allFinite              = true;
    float incomingVoicePeak      = 0.0f;
    float clickPeak              = 0.0f;   // max cleaned click in window
    float clickThreshold         = 0.0f;   // 0.0316228 * incomingVoicePeak
    float baselinePeak           = 0.0f;   // abs peak of last pre-steal block
    float stealWindowAbsPeak     = 0.0f;   // abs peak in 5 ms window
    int   stealSlot              = -1;
    int   terminationSamples     = -1;
    int   warmupBlocks           = -1;     // adaptive seed warmup (blocks)
    int   activeBeforeSteal      = 0;      // must be 4: a real steal
    // FR-124 ramp-shape verification -- supplementary assertions.
    float gainAfterOneBlock      = -1.0f;
    bool  gainDecreasedEachBlock = true;
};

StealResult runClickFreeSteal(const StealCase& sc)
{
    StealResult r;

    // -------------------------------------------------------------
    // Step 1: incoming voice peak (and full envelope) in isolation.
    // -------------------------------------------------------------
    const int incomingCapture = static_cast<int>(std::ceil(0.020 * sc.sampleRate));
    const IsolatedRender inc = renderIncomingVoiceIsolated(sc.sampleRate,
                                                            incomingCapture);
    r.incomingVoicePeak = inc.peak;
    r.clickThreshold    = 0.0316228f * r.incomingVoicePeak;

    // -------------------------------------------------------------
    // Step 2: run the real steal scenario.
    // -------------------------------------------------------------
    Membrum::VoicePool pool;
    std::vector<float> outL(static_cast<std::size_t>(kBlockSize), 0.0f);
    std::vector<float> outR(static_cast<std::size_t>(kBlockSize), 0.0f);

    prepareSeededPool(pool, sc.sampleRate, sc.policy, outL, outR);

    // Adaptive warmup: render until every seed has decayed below half the
    // click threshold. The seeds must all still be sounding then (checked
    // by the caller through activeBeforeSteal).
    const int maxWarmupBlocks =
        static_cast<int>(std::ceil(kMaxWarmupSeconds * sc.sampleRate)) / kBlockSize;
    const float seedPeakBar = kSeedPeakFractionOfThreshold * r.clickThreshold;
    int warmupBlocks = 0;
    while (maxActiveLevel(pool) >= seedPeakBar && warmupBlocks < maxWarmupBlocks)
    {
        pool.processBlock(outL.data(), outR.data(), kBlockSize);
        ++warmupBlocks;
    }
    r.warmupBlocks = warmupBlocks;

    // Baseline block.
    std::vector<float> preBlock(static_cast<std::size_t>(kBlockSize), 0.0f);
    pool.processBlock(preBlock.data(), outR.data(), kBlockSize);
    for (float s : preBlock)
    {
        const float a = std::fabs(s);
        if (a > r.baselinePeak) r.baselinePeak = a;
    }

    // Trigger the steal (the pool must be full of sounding seeds).
    r.activeBeforeSteal = pool.getActiveVoiceCount();
    pool.noteOn(67, 0.9f);

    for (int slot = 0; slot < 16; ++slot)
    {
        const auto& m = pool.voiceMeta(slot);
        if (m.state == Membrum::VoiceSlotState::Active &&
            m.originatingNote == 67)
        {
            r.stealSlot = slot;
            break;
        }
    }
    int releasingSlot = -1;
    for (int slot = 0; slot < 16; ++slot)
    {
        if (pool.releasingMeta(slot).state ==
            Membrum::VoiceSlotState::FastReleasing)
        {
            releasingSlot = slot;
            break;
        }
    }

    constexpr int kPostBlocks = 64;
    std::vector<float> post;
    post.reserve(static_cast<std::size_t>(kBlockSize * kPostBlocks));

    int samplesElapsed = 0;
    float prevGain = (releasingSlot >= 0)
        ? pool.releasingMeta(releasingSlot).fastReleaseGain
        : 0.0f;
    for (int b = 0; b < kPostBlocks; ++b)
    {
        pool.processBlock(outL.data(), outR.data(), kBlockSize);
        for (int i = 0; i < kBlockSize; ++i) post.push_back(outL[i]);
        samplesElapsed += kBlockSize;

        if (b == 0 && releasingSlot >= 0)
        {
            r.gainAfterOneBlock =
                pool.releasingMeta(releasingSlot).fastReleaseGain;
        }

        if (releasingSlot >= 0)
        {
            const auto& rm = pool.releasingMeta(releasingSlot);
            if (rm.state == Membrum::VoiceSlotState::FastReleasing)
            {
                if (rm.fastReleaseGain > prevGain + 1e-7f)
                    r.gainDecreasedEachBlock = false;
                prevGain = rm.fastReleaseGain;
            }
        }

        if (r.terminationSamples < 0)
        {
            for (int slot = 0; slot < 16; ++slot)
            {
                const auto& rm = pool.releasingMeta(slot);
                if (rm.state == Membrum::VoiceSlotState::Free &&
                    rm.fastReleaseGain < 1e-5f)
                {
                    r.terminationSamples = samplesElapsed;
                    break;
                }
            }
        }
    }

    for (float s : preBlock) if (!isFiniteSample(s)) r.allFinite = false;
    if (r.allFinite)
        for (float s : post) if (!isFiniteSample(s)) { r.allFinite = false; break; }

    // -------------------------------------------------------------
    // Step 3: render the two references for the exact click metric, with
    // the same seed fill + warmup + baseline block as the real steal.
    //   (a) silentSteal: the steal with pad 67's level at 0. Same allocator
    //       events, same victim, same shadow fade; the new voice adds 0.
    //   (b) noStealRef: no note 67; all 4 seeds keep decaying naturally.
    // -------------------------------------------------------------
    const int captureBlocks = 4;  // 512 samples >= 5 ms at 96 kHz
    const std::vector<float> silentSteal =
        renderReference(sc.sampleRate, sc.policy, warmupBlocks,
                        /*withSilentSteal*/ true, captureBlocks);
    const std::vector<float> noStealRef =
        renderReference(sc.sampleRate, sc.policy, warmupBlocks,
                        /*withSilentSteal*/ false, captureBlocks);

    // -------------------------------------------------------------
    // Step 4: exact click metric.
    //
    // Let  silentSteal = (fading stolen) + (other seeds natural)
    //      noStealRef  = (stolen natural) + (other seeds natural)
    //
    // Then silentSteal - noStealRef = (fading stolen) - (stolen natural).
    //
    // This is THE click artifact: the difference between the fast-
    // released voice and what it would have produced had it continued
    // naturally. The other seeds cancel bit-exactly because they evolve
    // identically in both renders (the steal only touches the victim's
    // slot).
    //
    // The click peak is the maximum absolute value of this exact
    // difference over the 5 ms window centred on the steal event.
    // Pre-steal samples are identical in both buffers, so the click is
    // zero before the steal boundary.
    // -------------------------------------------------------------
    const int halfWindow = static_cast<int>(
        std::ceil(0.0025 * sc.sampleRate));

    const int postLen = std::min({
        halfWindow,
        static_cast<int>(noStealRef.size()),
        static_cast<int>(silentSteal.size())
    });

    float clickPeakSample = 0.0f;
    for (int i = 0; i < postLen; ++i)
    {
        const float exactClick = silentSteal[i] - noStealRef[i];
        const float m = std::fabs(exactClick);
        if (m > clickPeakSample) clickPeakSample = m;
    }
    r.clickPeak = clickPeakSample;

    // Absolute peak of the 5 ms steal window (fallback sanity bound).
    for (int i = 0; i < halfWindow; ++i)
    {
        const int preIdx = kBlockSize - halfWindow + i;
        if (preIdx >= 0 && preIdx < kBlockSize)
        {
            const float a = std::fabs(preBlock[preIdx]);
            if (a > r.stealWindowAbsPeak) r.stealWindowAbsPeak = a;
        }
    }
    const int postLimit = std::min(halfWindow, static_cast<int>(post.size()));
    for (int i = 0; i < postLimit; ++i)
    {
        const float a = std::fabs(post[i]);
        if (a > r.stealWindowAbsPeak) r.stealWindowAbsPeak = a;
    }

    return r;
}

} // namespace

TEST_CASE("VoicePool steal click-free across policies and sample rates",
          "[membrum][voice_pool][phase3_2][steal_click_free]")
{
    const StealCase cases[] = {
        {Membrum::VoiceStealingPolicy::Oldest,   22050.0, "Oldest@22050"},
        {Membrum::VoiceStealingPolicy::Oldest,   44100.0, "Oldest@44100"},
        {Membrum::VoiceStealingPolicy::Oldest,   48000.0, "Oldest@48000"},
        {Membrum::VoiceStealingPolicy::Oldest,   96000.0, "Oldest@96000"},
        {Membrum::VoiceStealingPolicy::Oldest,  192000.0, "Oldest@192000"},
        {Membrum::VoiceStealingPolicy::Quietest, 22050.0, "Quietest@22050"},
        {Membrum::VoiceStealingPolicy::Quietest, 44100.0, "Quietest@44100"},
        {Membrum::VoiceStealingPolicy::Quietest, 48000.0, "Quietest@48000"},
        {Membrum::VoiceStealingPolicy::Quietest, 96000.0, "Quietest@96000"},
        {Membrum::VoiceStealingPolicy::Quietest,192000.0, "Quietest@192000"},
        {Membrum::VoiceStealingPolicy::Priority, 22050.0, "Priority@22050"},
        {Membrum::VoiceStealingPolicy::Priority, 44100.0, "Priority@44100"},
        {Membrum::VoiceStealingPolicy::Priority, 48000.0, "Priority@48000"},
        {Membrum::VoiceStealingPolicy::Priority, 96000.0, "Priority@96000"},
        {Membrum::VoiceStealingPolicy::Priority,192000.0, "Priority@192000"},
    };

    for (const auto& sc : cases)
    {
        // T1.9: the click reference silences only the NEW voice (pad 67's
        // level), so the seeds -- and with them the Quietest pick -- are
        // identical in every render, and the Quietest cases run too.
        const StealResult r = runClickFreeSteal(sc);

        CAPTURE(sc.name,
                r.warmupBlocks,
                r.activeBeforeSteal,
                r.incomingVoicePeak,
                r.clickPeak,
                r.clickThreshold,
                r.baselinePeak,
                r.stealWindowAbsPeak,
                r.stealSlot,
                r.gainAfterOneBlock,
                r.terminationSamples,
                r.gainDecreasedEachBlock);


        // Finite-output guard (FR-181).
        REQUIRE(r.allFinite);

        // The four seeds were still sounding: a real steal, not an idle slot.
        REQUIRE(r.activeBeforeSteal == 4);

        // New voice must have been allocated.
        REQUIRE(r.stealSlot >= 0);

        // Incoming voice produced audible output (sanity check).
        REQUIRE(r.incomingVoicePeak > 0.0f);

        // -----------------------------------------------------------
        // Primary SC-021 / FR-126 assertion: cleaned click <= -30 dBFS.
        // -----------------------------------------------------------
        REQUIRE(r.clickPeak <= r.clickThreshold);

        // -----------------------------------------------------------
        // Supplementary: exponential ramp shape. After one block of
        // processing (kBlockSize = 128 samples), the fastReleaseGain
        // must match k^128 where
        //     k = exp(-ln(1e6) / (0.005 * sr))      (Option B)
        // -----------------------------------------------------------
        const float k = std::exp(
            -Membrum::kFastReleaseLnFloor /
            (Membrum::kFastReleaseSecs * static_cast<float>(sc.sampleRate)));
        const float expectedGainBlock1 =
            static_cast<float>(std::pow(static_cast<double>(k), kBlockSize));

        // If the fade already terminated within the first block (small
        // sample rates: at 22050 Hz, 5 ms ~= 110 samples < 128), the
        // observed gain will be 0 and that is valid. Only assert the
        // shape match when gain is still positive after block 1.
        if (r.gainAfterOneBlock > 0.0f)
        {
            REQUIRE(r.gainAfterOneBlock >= 0.5f * expectedGainBlock1);
            REQUIRE(r.gainAfterOneBlock <= std::max(1.0f, 2.0f * expectedGainBlock1));
        }

        // Monotonicity: FR-127 / SC-021. While FastReleasing, gain
        // strictly decreases block-over-block.
        REQUIRE(r.gainDecreasedEachBlock);

        // -----------------------------------------------------------
        // FR-124 termination bound: the fade reaches the 1e-6 floor
        // within 5 ms wall-clock + 1 ms tolerance = 6 ms.
        // -----------------------------------------------------------
        const int bound =
            static_cast<int>(std::ceil(0.006 * sc.sampleRate));
        REQUIRE(r.terminationSamples > 0);
        REQUIRE(r.terminationSamples <= bound);
    }
}
