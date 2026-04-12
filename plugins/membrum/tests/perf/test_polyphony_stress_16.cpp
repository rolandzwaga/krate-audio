// ==============================================================================
// Phase 3.5 -- 16-voice stress test (SC-024 / FR-161 / FR-186)
// ==============================================================================
// 16 voices @ 44.1 kHz / 128-sample blocks for 10 seconds continuous.
// Random note-ons every ~5 ms, worst-case patch (Feedback + NoiseBody +
// ToneShaper + UnnaturalZone). Asserts:
//   (a) zero xruns (wall-clock per block < audio wall-clock per block)
//   (b) zero NaN/Inf in output
//
// Tagged [.perf] so it is skipped by default. Run with:
//   membrum_tests.exe "[.perf]"
//
// IMPORTANT (testing-guide ANTI-PATTERNS #13):
//   * No REQUIRE/CHECK inside sample-processing loops.
//   * NaN/Inf detection via bit manipulation (fast-math compat).
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "dsp/body_model_type.h"
#include "dsp/drum_voice.h"
#include "dsp/exciter_type.h"
#include "voice_pool/voice_pool.h"
#include "../unit/voice_pool/voice_pool_test_helpers.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

namespace {

constexpr double kSampleRate      = 44100.0;
constexpr int    kBlockSize       = 128;
constexpr double kDurationSeconds = 10.0;
constexpr int    kTotalBlocks     =
    static_cast<int>((kSampleRate * kDurationSeconds) / kBlockSize);
constexpr int    kPolyStress      = 16;

inline bool isNaNOrInfBits(float x) noexcept
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &x, sizeof(bits));
    return (bits & 0x7F800000u) == 0x7F800000u;
}

void configureStressPool(Membrum::VoicePool& pool)
{
    Membrum::TestHelpers::setAllPadsVoiceParams(pool, 0.5f, 0.5f, 0.7f, 0.3f, 0.8f);
    Membrum::TestHelpers::setAllPadsExciterType(pool, Membrum::ExciterType::Feedback);
    Membrum::TestHelpers::setAllPadsBodyModel(pool, Membrum::BodyModelType::NoiseBody);

    pool.forEachMainVoice([&](Membrum::DrumVoice& voice) {
        voice.setMaterial(0.5f);
        voice.setSize(0.5f);
        voice.setDecay(0.7f);
        voice.setStrikePosition(0.3f);
        voice.setLevel(0.8f);
        voice.setExciterType(Membrum::ExciterType::Feedback);
        voice.setBodyModel(Membrum::BodyModelType::NoiseBody);

        auto& ts = voice.toneShaper();
        auto& uz = voice.unnaturalZone();

        ts.setFilterType(Membrum::ToneShaperFilterType::Lowpass);
        ts.setFilterCutoff(3000.0f);
        ts.setFilterResonance(0.5f);
        ts.setFilterEnvAmount(0.6f);
        ts.setFilterEnvAttackMs(1.0f);
        ts.setFilterEnvDecayMs(150.0f);
        ts.setFilterEnvSustain(0.2f);
        ts.setFilterEnvReleaseMs(200.0f);
        ts.setDriveAmount(0.5f);
        ts.setFoldAmount(0.3f);
        ts.setPitchEnvStartHz(160.0f);
        ts.setPitchEnvEndHz(50.0f);
        ts.setPitchEnvTimeMs(20.0f);
        ts.setPitchEnvCurve(Membrum::ToneShaperCurve::Exponential);

        uz.setModeStretch(1.2f);
        uz.setDecaySkew(0.3f);
        uz.modeInject.setAmount(0.3f);
        uz.nonlinearCoupling.setAmount(0.3f);
        uz.materialMorph.setEnabled(true);
        uz.materialMorph.setStart(0.2f);
        uz.materialMorph.setEnd(0.8f);
        uz.materialMorph.setDurationMs(300.0f);
    });
}

} // namespace

// ==============================================================================
// SC-024 / FR-161 : 16-voice 10 s stress run, zero xruns
// ==============================================================================
TEST_CASE("Phase35 Stress: 16 voices 10 s zero xruns",
          "[.perf][membrum][phase3_5][stress][cpu]")
{
    Membrum::VoicePool pool;
    pool.prepare(kSampleRate, kBlockSize);
    pool.setMaxPolyphony(kPolyStress);
    pool.setVoiceStealingPolicy(Membrum::VoiceStealingPolicy::Oldest);
    configureStressPool(pool);

    std::vector<float> outL(static_cast<std::size_t>(kBlockSize), 0.0f);
    std::vector<float> outR(static_cast<std::size_t>(kBlockSize), 0.0f);

    // Prime pool with 16 concurrent note-ons so every slot is sounding from
    // the start.
    for (int v = 0; v < kPolyStress; ++v)
    {
        const auto note = static_cast<std::uint8_t>(36 + v);
        pool.noteOn(note, 0.8f);
    }

    std::mt19937 rng(0xDEADBEEFu);
    std::uniform_int_distribution<int>    noteDist(36, 67);
    std::uniform_real_distribution<float> velDist(0.1f, 1.0f);

    const auto blockAudioDuration =
        std::chrono::duration<double>(static_cast<double>(kBlockSize) / kSampleRate);

    constexpr int kNoteOnPeriodBlocks = 4; // ~5 ms at 44.1 kHz / 128-block

    int  xruns         = 0;
    bool hasNaNOrInf   = false;
    double totalWallSec = 0.0;

    const auto overallStart = std::chrono::steady_clock::now();

    for (int b = 0; b < kTotalBlocks; ++b)
    {
        if ((b % kNoteOnPeriodBlocks) == 0)
        {
            const auto n1 = static_cast<std::uint8_t>(noteDist(rng));
            pool.noteOn(n1, velDist(rng));
        }

        const auto blockStart = std::chrono::steady_clock::now();
        pool.processBlock(outL.data(), outR.data(), kBlockSize);
        const auto blockEnd = std::chrono::steady_clock::now();

        const auto blockElapsed =
            std::chrono::duration<double>(blockEnd - blockStart);
        if (blockElapsed > blockAudioDuration)
            ++xruns;

        if (!hasNaNOrInf)
        {
            for (int i = 0; i < kBlockSize; ++i)
            {
                if (isNaNOrInfBits(outL[static_cast<std::size_t>(i)]) ||
                    isNaNOrInfBits(outR[static_cast<std::size_t>(i)]))
                {
                    hasNaNOrInf = true;
                    break;
                }
            }
        }
    }

    const auto overallEnd = std::chrono::steady_clock::now();
    totalWallSec =
        std::chrono::duration<double>(overallEnd - overallStart).count();
    const double cpuPercent = (totalWallSec / kDurationSeconds) * 100.0;

    std::printf(
        "\n[phase35-stress16] poly=%d -> %.3f%% CPU, xruns=%d, blocks=%d\n",
        kPolyStress, cpuPercent, xruns, kTotalBlocks);
    std::fflush(stdout);

    INFO("16-voice stress: cpu=" << cpuPercent << "% xruns=" << xruns
                                  << " blocks=" << kTotalBlocks);

    CHECK_FALSE(hasNaNOrInf);
    CHECK(xruns == 0);
}
