// ==============================================================================
// Noise Body CPU budget (Phase 9 — T124, FR-062)
// ==============================================================================
// Measures single-voice CPU for the worst-case Noise Body configuration:
//
//     Impulse exciter + NoiseBody + Tone Shaper ON + Unnatural Zone ON
//
// NoiseBody starts at kModeCount = 40 (research.md §4.6). If this test fails
// the 1.25% budget (SC-003), NoiseBody::kNoiseBodyModeCount MUST be reduced —
// 40 → 30 → 25 → 20 — until the budget is met, and the chosen value
// documented in noise_body.h citing FR-062 (tasks.md T128).
//
// Tagged [.perf]: hidden by default; run explicitly with
//     membrum_tests.exe "[.perf]"
//
// NO REQUIRE/CHECK inside the sample loop (testing-guide ANTI-PATTERNS #13).
// NaN/Inf check via bit manipulation (CLAUDE.md cross-platform rule).
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "dsp/body_model_type.h"
#include "dsp/bodies/noise_body.h"
#include "dsp/drum_voice.h"
#include "dsp/exciter_type.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

constexpr double kSampleRate      = 44100.0;
constexpr double kDurationSeconds = 10.0;
constexpr int    kTotalSamples    =
    static_cast<int>(kSampleRate * kDurationSeconds);
constexpr int    kBlockSize       = 64;
constexpr float  kVelocity100     = 100.0f / 127.0f;
constexpr int    kRetriggerEverySamples =
    static_cast<int>(kSampleRate * 0.25);
constexpr double kBudgetPercent   = 1.25;

inline bool isNaNOrInfBits(float x) noexcept
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &x, sizeof(bits));
    return (bits & 0x7F800000u) == 0x7F800000u;
}

} // namespace

TEST_CASE("NoiseBodyCPU: Impulse + NoiseBody + toneShaper + unnatural "
          "at kModeCount=40 stays under 1.25% CPU",
          "[.perf][membrum][benchmark][cpu][noise_body]")
{
    // Documented starting point per FR-062.
    static_assert(Membrum::NoiseBody::kModeCount <= 40,
                  "NoiseBody::kModeCount must start at 40 or below; "
                  "reductions must be documented in noise_body.h citing FR-062.");

    Membrum::DrumVoice voice;
    voice.prepare(kSampleRate, 0u);
    voice.setMaterial(0.7f);
    voice.setSize(0.4f);
    voice.setDecay(0.7f);
    voice.setStrikePosition(0.3f);
    voice.setLevel(0.8f);
    voice.setExciterType(Membrum::ExciterType::Impulse);
    voice.setBodyModel(Membrum::BodyModelType::NoiseBody);

    auto& ts = voice.toneShaper();
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

    auto& uz = voice.unnaturalZone();
    uz.setModeStretch(1.2f);
    uz.setDecaySkew(0.3f);
    uz.modeInject.setAmount(0.3f);
    uz.nonlinearCoupling.setAmount(0.3f);
    uz.materialMorph.setEnabled(true);
    uz.materialMorph.setStart(0.2f);
    uz.materialMorph.setEnd(0.8f);
    uz.materialMorph.setDurationMs(300.0f);

    std::array<float, kBlockSize> block{};
    bool hasNaNOrInf = false;

    voice.noteOn(kVelocity100);

    int samplesSinceTrigger = 0;
    int samplesRemaining    = kTotalSamples;

    const auto start = std::chrono::steady_clock::now();
    while (samplesRemaining > 0)
    {
        const int thisBlock =
            samplesRemaining < kBlockSize ? samplesRemaining : kBlockSize;

        voice.processBlock(block.data(), thisBlock);

        if (!hasNaNOrInf)
        {
            for (int i = 0; i < thisBlock; ++i)
            {
                if (isNaNOrInfBits(block[static_cast<std::size_t>(i)]))
                {
                    hasNaNOrInf = true;
                    break;
                }
            }
        }

        samplesRemaining   -= thisBlock;
        samplesSinceTrigger += thisBlock;
        if (samplesSinceTrigger >= kRetriggerEverySamples)
        {
            voice.noteOn(kVelocity100);
            samplesSinceTrigger = 0;
        }
    }
    const auto end = std::chrono::steady_clock::now();

    const double wallSeconds =
        std::chrono::duration<double>(end - start).count();
    const double cpuPercent = (wallSeconds / kDurationSeconds) * 100.0;

    std::printf(
        "\n[noise_body_cpu] NoiseBody kModeCount=%d -> %.3f%% CPU "
        "(budget=%.2f%%)\n",
        Membrum::NoiseBody::kModeCount, cpuPercent, kBudgetPercent);
    std::fflush(stdout);

    INFO("NoiseBody kModeCount=" << Membrum::NoiseBody::kModeCount
         << " cpu=" << cpuPercent << "%");
    CHECK_FALSE(hasNaNOrInf);
    CHECK(cpuPercent <= kBudgetPercent);
}
