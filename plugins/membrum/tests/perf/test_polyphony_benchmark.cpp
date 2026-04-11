// ==============================================================================
// Phase 3.5 -- 8-voice polyphony CPU benchmark (SC-023 / FR-160 / FR-186)
// ==============================================================================
// Measures 8-voice worst-case CPU for Membrum's VoicePool at 44.1 kHz /
// 128-sample blocks over 10 seconds of continuous processing. Worst-case
// configuration per spec 138:
//
//   Exciter = Feedback
//   Body    = NoiseBody
//   ToneShaper = enabled (all stages active)
//   UnnaturalZone = enabled (mode inject, nonlinear coupling, material morph)
//
// Budget:
//   * ≤ 12% for non-waived combinations (FR-160 / SC-023)
//   * ≤ 18% for the Phase 2 waived cell Feedback+NoiseBody+TS+UN (SC-023)
//
// Also writes a CSV sidecar to
//   build/windows-x64-release/membrum_phase3_benchmark.csv
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

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr double kSampleRate      = 44100.0;
constexpr int    kBlockSize       = 128;
constexpr double kDurationSeconds = 10.0;
constexpr int    kTotalBlocks     =
    static_cast<int>((kSampleRate * kDurationSeconds) / kBlockSize);

constexpr double kBudgetPercent            = 12.0; // FR-160 / SC-023
constexpr double kWaivedCellBudgetPercent  = 18.0; // SC-023 Phase 2 waiver
constexpr int    kPolyBench                = 8;

constexpr const char* exciterName(Membrum::ExciterType t) noexcept
{
    switch (t)
    {
    case Membrum::ExciterType::Impulse:    return "Impulse";
    case Membrum::ExciterType::Mallet:     return "Mallet";
    case Membrum::ExciterType::NoiseBurst: return "NoiseBurst";
    case Membrum::ExciterType::Friction:   return "Friction";
    case Membrum::ExciterType::FMImpulse:  return "FMImpulse";
    case Membrum::ExciterType::Feedback:   return "Feedback";
    default:                               return "Unknown";
    }
}

constexpr const char* bodyName(Membrum::BodyModelType t) noexcept
{
    switch (t)
    {
    case Membrum::BodyModelType::Membrane:  return "Membrane";
    case Membrum::BodyModelType::Plate:     return "Plate";
    case Membrum::BodyModelType::Shell:     return "Shell";
    case Membrum::BodyModelType::String:    return "String";
    case Membrum::BodyModelType::Bell:      return "Bell";
    case Membrum::BodyModelType::NoiseBody: return "NoiseBody";
    default:                                return "Unknown";
    }
}

inline bool isNaNOrInfBits(float x) noexcept
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &x, sizeof(bits));
    return (bits & 0x7F800000u) == 0x7F800000u;
}

// Configure every main voice in the pool to run the worst-case Phase 2 patch.
// ToneShaper and UnnaturalZone are applied per voice via forEachMainVoice()
// because the shared params bundle only carries the Phase 1 continuous knobs.
void configurePoolWorstCase(Membrum::VoicePool& pool,
                            Membrum::ExciterType   ex,
                            Membrum::BodyModelType body,
                            bool                   toneShaperOn,
                            bool                   unnaturalOn)
{
    pool.setSharedVoiceParams(0.5f, 0.5f, 0.7f, 0.3f, 0.8f);
    pool.setSharedExciterType(ex);
    pool.setSharedBodyModel(body);

    pool.forEachMainVoice([&](Membrum::DrumVoice& voice) {
        voice.setMaterial(0.5f);
        voice.setSize(0.5f);
        voice.setDecay(0.7f);
        voice.setStrikePosition(0.3f);
        voice.setLevel(0.8f);
        voice.setExciterType(ex);
        voice.setBodyModel(body);

        auto& ts = voice.toneShaper();
        auto& uz = voice.unnaturalZone();

        if (toneShaperOn)
        {
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
        }
        else
        {
            ts.setFilterCutoff(20000.0f);
            ts.setFilterResonance(0.0f);
            ts.setFilterEnvAmount(0.0f);
            ts.setDriveAmount(0.0f);
            ts.setFoldAmount(0.0f);
            ts.setPitchEnvTimeMs(0.0f);
        }

        if (unnaturalOn)
        {
            uz.setModeStretch(1.2f);
            uz.setDecaySkew(0.3f);
            uz.modeInject.setAmount(0.3f);
            uz.nonlinearCoupling.setAmount(0.3f);
            uz.materialMorph.setEnabled(true);
            uz.materialMorph.setStart(0.2f);
            uz.materialMorph.setEnd(0.8f);
            uz.materialMorph.setDurationMs(300.0f);
        }
        else
        {
            uz.setModeStretch(1.0f);
            uz.setDecaySkew(0.0f);
            uz.modeInject.setAmount(0.0f);
            uz.nonlinearCoupling.setAmount(0.0f);
            uz.materialMorph.setEnabled(false);
        }
    });
}

struct PolyBenchResult
{
    int                    polyphony;
    Membrum::ExciterType   ex;
    Membrum::BodyModelType body;
    bool                   toneShaperOn;
    bool                   unnaturalOn;
    double                 cpuPercent;
    int                    xruns;
    bool                   hasNaNOrInf;
};

// Run the pool for `kTotalBlocks`, re-triggering voices every ~250 ms so
// all `poly` voices remain actively ringing across the entire run. Returns
// elapsed wall-clock seconds.
double benchmarkPool(Membrum::VoicePool& pool,
                     int                  poly,
                     bool&                outHasNaNOrInf,
                     int&                 outXruns)
{
    std::vector<float> outL(static_cast<std::size_t>(kBlockSize), 0.0f);
    std::vector<float> outR(static_cast<std::size_t>(kBlockSize), 0.0f);

    outHasNaNOrInf = false;
    outXruns       = 0;

    // Retrigger the full chord every ~250 ms so all `poly` voices stay busy.
    constexpr int kRetriggerEveryBlocks =
        static_cast<int>((kSampleRate * 0.25) / kBlockSize);

    // Prime the pool with `poly` concurrent note-ons.
    auto triggerChord = [&]() {
        for (int v = 0; v < poly; ++v)
        {
            const auto note = static_cast<std::uint8_t>(36 + v);
            pool.noteOn(note, 0.8f);
        }
    };

    triggerChord();

    // Wall-clock budget per audio block, for xrun detection.
    const auto blockAudioDuration =
        std::chrono::duration<double>(static_cast<double>(kBlockSize) / kSampleRate);

    int blocksSinceRetrigger = 0;
    const auto overallStart = std::chrono::steady_clock::now();

    for (int b = 0; b < kTotalBlocks; ++b)
    {
        const auto blockStart = std::chrono::steady_clock::now();
        pool.processBlock(outL.data(), outR.data(), kBlockSize);
        const auto blockEnd = std::chrono::steady_clock::now();

        const auto blockElapsed =
            std::chrono::duration<double>(blockEnd - blockStart);
        if (blockElapsed > blockAudioDuration)
            ++outXruns;

        if (!outHasNaNOrInf)
        {
            for (int i = 0; i < kBlockSize; ++i)
            {
                if (isNaNOrInfBits(outL[static_cast<std::size_t>(i)]) ||
                    isNaNOrInfBits(outR[static_cast<std::size_t>(i)]))
                {
                    outHasNaNOrInf = true;
                    break;
                }
            }
        }

        ++blocksSinceRetrigger;
        if (blocksSinceRetrigger >= kRetriggerEveryBlocks)
        {
            triggerChord();
            blocksSinceRetrigger = 0;
        }
    }

    const auto overallEnd = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(overallEnd - overallStart).count();
}

std::string isoTimestamp()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tmUtc{};
#if defined(_WIN32)
    gmtime_s(&tmUtc, &t);
#else
    gmtime_r(&t, &tmUtc);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf),
                  "%04d-%02d-%02dT%02d:%02d:%02dZ",
                  tmUtc.tm_year + 1900,
                  tmUtc.tm_mon + 1,
                  tmUtc.tm_mday,
                  tmUtc.tm_hour,
                  tmUtc.tm_min,
                  tmUtc.tm_sec);
    return std::string(buf);
}

void appendPhase3CsvRow(const PolyBenchResult& r, const std::string& timestamp)
{
    namespace fs = std::filesystem;
    const fs::path csvPath =
        fs::path("build") / "windows-x64-release" / "membrum_phase3_benchmark.csv";

    std::error_code ec;
    fs::create_directories(csvPath.parent_path(), ec);

    const bool exists = fs::exists(csvPath);
    std::ofstream out(csvPath, std::ios::app);
    if (!out)
    {
        std::fprintf(stderr,
                     "[bench3] warning: could not open %s for append\n",
                     csvPath.string().c_str());
        return;
    }
    if (!exists)
    {
        out << "timestamp,polyphony,exciter,body,toneShaper,unnatural,"
               "cpu_percent,xruns\n";
    }
    out << timestamp << ','
        << r.polyphony << ','
        << exciterName(r.ex) << ','
        << bodyName(r.body) << ','
        << (r.toneShaperOn ? "on" : "off") << ','
        << (r.unnaturalOn  ? "on" : "off") << ','
        << r.cpuPercent << ','
        << r.xruns << '\n';
}

} // namespace

// ==============================================================================
// SC-023 / FR-160 / FR-186 : 8-voice worst-case CPU benchmark
// ==============================================================================
TEST_CASE("Phase35 Bench: 8-voice worst case under 12% CPU",
          "[.perf][membrum][phase3_5][benchmark][cpu]")
{
    const auto ex   = Membrum::ExciterType::Feedback;
    const auto body = Membrum::BodyModelType::NoiseBody;
    const bool tsOn = true;
    const bool unOn = true;

    Membrum::VoicePool pool;
    pool.prepare(kSampleRate, kBlockSize);
    pool.setMaxPolyphony(kPolyBench);
    pool.setVoiceStealingPolicy(Membrum::VoiceStealingPolicy::Oldest);
    configurePoolWorstCase(pool, ex, body, tsOn, unOn);

    bool hasNaNOrInf = false;
    int  xruns       = 0;
    const double wallSeconds =
        benchmarkPool(pool, kPolyBench, hasNaNOrInf, xruns);
    const double cpuPercent = (wallSeconds / kDurationSeconds) * 100.0;

    PolyBenchResult r{kPolyBench, ex, body, tsOn, unOn,
                      cpuPercent, xruns, hasNaNOrInf};
    appendPhase3CsvRow(r, isoTimestamp());

    std::printf(
        "\n[phase35-bench8] %s + %s (ts=%s un=%s) poly=%d -> "
        "%.3f%% CPU (budget=%.1f%%, waived=%.1f%%), xruns=%d\n",
        exciterName(ex), bodyName(body),
        tsOn ? "on" : "off", unOn ? "on" : "off",
        kPolyBench, cpuPercent, kBudgetPercent, kWaivedCellBudgetPercent,
        xruns);
    std::fflush(stdout);

    const bool isWaivedCell =
        (ex == Membrum::ExciterType::Feedback) &&
        (body == Membrum::BodyModelType::NoiseBody) &&
        tsOn && unOn;

    INFO("cell=" << exciterName(ex) << "+" << bodyName(body)
                 << " ts=" << (tsOn ? "on" : "off")
                 << " un=" << (unOn ? "on" : "off")
                 << " poly=" << kPolyBench
                 << " cpu=" << cpuPercent << "% xruns=" << xruns);

    CHECK_FALSE(hasNaNOrInf);
    if (isWaivedCell)
        CHECK(cpuPercent <= kWaivedCellBudgetPercent);
    else
        CHECK(cpuPercent <= kBudgetPercent);
}
