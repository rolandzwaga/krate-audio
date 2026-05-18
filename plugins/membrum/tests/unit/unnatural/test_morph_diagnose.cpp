// Phase 11 diagnostic: render the exact user scenario (hit pad 0 with morph
// enabled at two XY positions) and report per-segment RMS in dBFS so we can
// see how big the audible delta actually is. Also dumps WAV files for
// manual A/B listening.

#include <catch2/catch_test_macros.hpp>

#include "voice_pool/voice_pool.h"
#include "dsp/pad_config.h"
#include "unit/voice_pool/voice_pool_test_helpers.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace Membrum;

namespace {

constexpr int    kBlock  = 256;
constexpr int    kBlocks = 64;        // ~371 ms at 44.1 kHz
constexpr double kSR     = 44100.0;

std::vector<float> renderHit(float morphStart, float morphEnd,
                              float noiseMix, float clickMix)
{
    VoicePool pool;
    pool.prepare(kSR, kBlock);
    pool.setMaxPolyphony(1);
    TestHelpers::setAllPadsBodyModel(pool, BodyModelType::Membrane);
    TestHelpers::setAllPadsExciterType(pool, ExciterType::Impulse);
    TestHelpers::setAllPadsVoiceParams(pool, 0.5f, 0.5f, 0.5f, 0.3f, 0.8f);

    pool.setPadConfigField(0, kPadMorphEnabled,  1.0f);
    pool.setPadConfigField(0, kPadMorphStart,    morphStart);
    pool.setPadConfigField(0, kPadMorphEnd,      morphEnd);
    pool.setPadConfigField(0, kPadMorphDuration, 0.095477f);
    pool.setPadConfigField(0, kPadNoiseLayerMix, noiseMix);
    pool.setPadConfigField(0, kPadClickLayerMix, clickMix);

    pool.noteOn(36, 0.9f);

    std::vector<float> out(static_cast<std::size_t>(kBlocks * kBlock), 0.0f);
    std::array<float, kBlock> outL{}, outR{};
    for (int b = 0; b < kBlocks; ++b)
    {
        outL.fill(0.0f); outR.fill(0.0f);
        pool.processBlock(outL.data(), outR.data(), kBlock);
        for (int i = 0; i < kBlock; ++i)
            out[static_cast<std::size_t>(b * kBlock + i)] = outL[i];
    }
    return out;
}

double segRmsDb(const std::vector<float>& s, int start, int end)
{
    double sumSq = 0.0;
    int n = 0;
    for (int i = start; i < end && i < static_cast<int>(s.size()); ++i, ++n)
        sumSq += static_cast<double>(s[i]) * s[i];
    if (n == 0) return -200.0;
    const double rms = std::sqrt(sumSq / n);
    return 20.0 * std::log10(rms + 1e-30);
}

double rmsDiffDb(const std::vector<float>& a, const std::vector<float>& b)
{
    double sumSq = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        const double d = static_cast<double>(a[i]) - static_cast<double>(b[i]);
        sumSq += d * d;
    }
    const double rms = std::sqrt(sumSq / a.size());
    return 20.0 * std::log10(rms + 1e-30);
}

void writeWav(const std::string& path, const std::vector<float>& samples)
{
    const std::uint32_t numSamples = static_cast<std::uint32_t>(samples.size());
    const std::uint32_t byteRate   = static_cast<std::uint32_t>(kSR) * 1 * 2;
    const std::uint32_t dataSize   = numSamples * 2;
    std::ofstream f(path, std::ios::binary);
    f.write("RIFF", 4);
    std::uint32_t chunkSize = 36 + dataSize;
    f.write(reinterpret_cast<const char*>(&chunkSize), 4);
    f.write("WAVEfmt ", 8);
    std::uint32_t fmtSize = 16;
    f.write(reinterpret_cast<const char*>(&fmtSize), 4);
    std::uint16_t fmt = 1, nch = 1, bps = 16;
    f.write(reinterpret_cast<const char*>(&fmt), 2);
    f.write(reinterpret_cast<const char*>(&nch), 2);
    std::uint32_t srate = static_cast<std::uint32_t>(kSR);
    f.write(reinterpret_cast<const char*>(&srate), 4);
    f.write(reinterpret_cast<const char*>(&byteRate), 4);
    std::uint16_t ba = 2;
    f.write(reinterpret_cast<const char*>(&ba), 2);
    f.write(reinterpret_cast<const char*>(&bps), 2);
    f.write("data", 4);
    f.write(reinterpret_cast<const char*>(&dataSize), 4);
    for (float s : samples)
    {
        const std::int16_t i = static_cast<std::int16_t>(
            std::max(-1.0f, std::min(1.0f, s)) * 32767.0f);
        f.write(reinterpret_cast<const char*>(&i), 2);
    }
}

} // namespace

TEST_CASE("MorphDiagnose: user preset reproduction",
          "[membrum][morph][diagnose-user][.diagnose]")
{
    // Pad 0 cfg captured from the user's live trace log on 2026-05-18.
    auto renderUserPreset = [&](float mS, float mE) {
        VoicePool pool;
        pool.prepare(kSR, kBlock);
        pool.setMaxPolyphony(1);
        for (int p = 0; p < kNumPads; ++p)
        {
            pool.setPadConfigSelector(p, kPadExciterType,
                static_cast<int>(ExciterType::Impulse));
            pool.setPadConfigSelector(p, kPadBodyModel,
                static_cast<int>(BodyModelType::Membrane));
        }
        pool.setPadConfigField(0, kPadMaterial,            0.180f);
        pool.setPadConfigField(0, kPadSize,                0.760f);
        pool.setPadConfigField(0, kPadDecay,               0.210f);
        pool.setPadConfigField(0, kPadStrikePosition,      0.300f);
        pool.setPadConfigField(0, kPadLevel,               0.850f);
        pool.setPadConfigField(0, kPadMorphEnabled,        1.000f);
        pool.setPadConfigField(0, kPadMorphStart,          mS);
        pool.setPadConfigField(0, kPadMorphEnd,            mE);
        pool.setPadConfigField(0, kPadMorphDuration,       0.095f);
        pool.setPadConfigField(0, kPadNoiseLayerMix,       0.050f);
        pool.setPadConfigField(0, kPadClickLayerMix,       0.550f);
        pool.setPadConfigField(0, kPadTSDriveAmount,       0.200f);
        pool.setPadConfigField(0, kPadTSFoldAmount,        0.000f);
        pool.setPadConfigField(0, kPadTSFilterCutoff,      0.285f);
        pool.setPadConfigField(0, kPadTSFilterEnvAmount,   0.500f);
        pool.setPadConfigField(0, kPadModeStretch,         0.323f);
        pool.setPadConfigField(0, kPadDecaySkew,           0.500f);

        pool.noteOn(36, 0.9f);

        std::vector<float> out(static_cast<std::size_t>(kBlocks * kBlock), 0.0f);
        std::array<float, kBlock> outL{}, outR{};
        for (int b = 0; b < kBlocks; ++b)
        {
            outL.fill(0.0f); outR.fill(0.0f);
            pool.processBlock(outL.data(), outR.data(), kBlock);
            for (int i = 0; i < kBlock; ++i)
                out[static_cast<std::size_t>(b * kBlock + i)] = outL[i];
        }
        return out;
    };

    constexpr int e1 = static_cast<int>(0.050 * kSR);
    constexpr int e2 = static_cast<int>(0.200 * kSR);
    constexpr int e3 = static_cast<int>(0.371 * kSR);

    auto stat = [&](const std::vector<float>& s, const char* label) {
        const double r1 = segRmsDb(s, 0, e1);
        const double r2 = segRmsDb(s, e1, e2);
        const double r3 = segRmsDb(s, e2, e3);
        std::fprintf(stderr, "%-24s early=%6.2f  mid=%6.2f  late=%6.2f dBFS\n",
                     label, r1, r2, r3);
    };

    auto diff = [&](const std::vector<float>& a, const std::vector<float>& b,
                    const char* label) {
        std::vector<float> d(a.size());
        for (std::size_t i = 0; i < a.size(); ++i) d[i] = a[i] - b[i];
        const double r1 = segRmsDb(d, 0, e1);
        const double r2 = segRmsDb(d, e1, e2);
        const double r3 = segRmsDb(d, e2, e3);
        std::fprintf(stderr, "  %-22s early=%6.2f  mid=%6.2f  late=%6.2f dBFS\n",
                     label, r1, r2, r3);
    };

    std::fprintf(stderr, "\n=== User-preset pad 0 (material=0.18, size=0.76, decay=0.21, click=0.55, fcut=0.285) ===\n");
    auto a = renderUserPreset(0.0f, 0.0f);
    auto b = renderUserPreset(1.0f, 1.0f);
    auto c = renderUserPreset(0.0f, 1.0f);
    auto d = renderUserPreset(1.0f, 0.0f);
    stat(a, "morph(0,0)");
    stat(b, "morph(1,1)");
    stat(c, "morph(0,1) wood->metal");
    stat(d, "morph(1,0) metal->wood");
    std::fprintf(stderr, "\nDifference RMS per segment:\n");
    diff(a, b, "(0,0) vs (1,1):");
    diff(c, d, "(0,1) vs (1,0):");
    diff(a, c, "(0,0) vs (0,1):");
    diff(b, d, "(1,1) vs (1,0):");

    writeWav("F:/tmp/user_morph_00.wav", a);
    writeWav("F:/tmp/user_morph_11.wav", b);
    writeWav("F:/tmp/user_morph_01.wav", c);
    writeWav("F:/tmp/user_morph_10.wav", d);
    std::fprintf(stderr, "\nWAVs written to F:/tmp/user_morph_*.wav\n");

    // Extended render: 1 s, isolate the post-morph regime to confirm whether
    // morphEnd actually drives the body decay rate after the morph completes
    // (200 ms). morph (1,0) should die fast in the 400-1000 ms window;
    // morph (1,1) should keep ringing. If they're identical here, the
    // mid-note material refresh isn't actually re-damping the bank.
    auto renderLong = [&](float mS, float mE) {
        VoicePool pool;
        pool.prepare(kSR, kBlock);
        pool.setMaxPolyphony(1);
        for (int p = 0; p < kNumPads; ++p)
        {
            pool.setPadConfigSelector(p, kPadExciterType,
                static_cast<int>(ExciterType::Impulse));
            pool.setPadConfigSelector(p, kPadBodyModel,
                static_cast<int>(BodyModelType::Membrane));
        }
        pool.setPadConfigField(0, kPadMaterial,            0.180f);
        pool.setPadConfigField(0, kPadSize,                0.760f);
        pool.setPadConfigField(0, kPadDecay,               0.210f);
        pool.setPadConfigField(0, kPadStrikePosition,      0.300f);
        pool.setPadConfigField(0, kPadLevel,               0.850f);
        pool.setPadConfigField(0, kPadMorphEnabled,        1.000f);
        pool.setPadConfigField(0, kPadMorphStart,          mS);
        pool.setPadConfigField(0, kPadMorphEnd,            mE);
        pool.setPadConfigField(0, kPadMorphDuration,       0.095f);  // 200 ms
        pool.setPadConfigField(0, kPadNoiseLayerMix,       0.050f);
        pool.setPadConfigField(0, kPadClickLayerMix,       0.550f);
        pool.setPadConfigField(0, kPadTSDriveAmount,       0.200f);
        pool.setPadConfigField(0, kPadTSFoldAmount,        0.000f);
        pool.setPadConfigField(0, kPadTSFilterCutoff,      0.285f);
        pool.setPadConfigField(0, kPadTSFilterEnvAmount,   0.500f);
        pool.setPadConfigField(0, kPadModeStretch,         0.323f);
        pool.setPadConfigField(0, kPadDecaySkew,           0.500f);

        pool.noteOn(36, 0.9f);

        const int longBlocks = static_cast<int>(1.0 * kSR / kBlock);
        std::vector<float> out(static_cast<std::size_t>(longBlocks * kBlock), 0.0f);
        std::array<float, kBlock> outL{}, outR{};
        for (int b = 0; b < longBlocks; ++b)
        {
            outL.fill(0.0f); outR.fill(0.0f);
            pool.processBlock(outL.data(), outR.data(), kBlock);
            for (int i = 0; i < kBlock; ++i)
                out[static_cast<std::size_t>(b * kBlock + i)] = outL[i];
        }
        return out;
    };

    std::fprintf(stderr, "\n=== Extended 1 s render -- post-morph regime ===\n");
    auto l11 = renderLong(1.0f, 1.0f);
    auto l10 = renderLong(1.0f, 0.0f);
    auto l00 = renderLong(0.0f, 0.0f);

    const int p1 = static_cast<int>(0.250 * kSR);  // just after morph done
    const int p2 = static_cast<int>(0.500 * kSR);  // 500 ms
    const int p3 = static_cast<int>(0.750 * kSR);  // 750 ms
    const int p4 = static_cast<int>(1.000 * kSR);  // 1 s

    std::fprintf(stderr, "morph(1,1)  250-500ms = %5.2f  500-750ms = %5.2f  750-1000ms = %5.2f dBFS\n",
        segRmsDb(l11, p1, p2), segRmsDb(l11, p2, p3), segRmsDb(l11, p3, p4));
    std::fprintf(stderr, "morph(1,0)  250-500ms = %5.2f  500-750ms = %5.2f  750-1000ms = %5.2f dBFS\n",
        segRmsDb(l10, p1, p2), segRmsDb(l10, p2, p3), segRmsDb(l10, p3, p4));
    std::fprintf(stderr, "morph(0,0)  250-500ms = %5.2f  500-750ms = %5.2f  750-1000ms = %5.2f dBFS\n",
        segRmsDb(l00, p1, p2), segRmsDb(l00, p2, p3), segRmsDb(l00, p3, p4));

    writeWav("F:/tmp/user_morph_1s_11.wav", l11);
    writeWav("F:/tmp/user_morph_1s_10.wav", l10);
    writeWav("F:/tmp/user_morph_1s_00.wav", l00);

    // Back-to-back A/B (1s gap with a hit at 0, then a hit at 1.5s).
    // Hear hit_11 followed shortly by hit_10 -- the late tails will diverge.
    std::vector<float> ab;
    ab.insert(ab.end(), l11.begin(), l11.end());
    std::vector<float> silence(static_cast<std::size_t>(0.25 * kSR), 0.0f);
    ab.insert(ab.end(), silence.begin(), silence.end());
    ab.insert(ab.end(), l10.begin(), l10.end());
    ab.insert(ab.end(), silence.begin(), silence.end());
    ab.insert(ab.end(), l00.begin(), l00.end());
    writeWav("F:/tmp/user_morph_ab.wav", ab);
    std::fprintf(stderr, "\n1s WAVs + A/B at F:/tmp/user_morph_1s_*.wav and user_morph_ab.wav\n");
}

TEST_CASE("MorphDiagnose: print and dump XY pad audibility data",
          "[membrum][morph][diagnose][.diagnose]")
{
    constexpr int earlyEnd  = static_cast<int>(0.050 * kSR);
    constexpr int midStart  = static_cast<int>(0.050 * kSR);
    constexpr int midEnd    = static_cast<int>(0.200 * kSR);
    constexpr int lateStart = static_cast<int>(0.200 * kSR);
    constexpr int lateEnd   = static_cast<int>(0.371 * kSR);

    struct Config { float ns; float cl; const char* label; };
    const std::array<Config, 2> configs{
        Config{0.0f,  0.0f,  "(noise/click ZEROED)"},
        Config{0.35f, 0.5f,  "(noise/click DEFAULT)"},
    };

    for (const auto& cfg : configs)
    {
        std::fprintf(stderr, "\n=== %s ===\n", cfg.label);
        auto a = renderHit(0.0f, 1.0f, cfg.ns, cfg.cl);
        auto b = renderHit(1.0f, 0.0f, cfg.ns, cfg.cl);
        std::fprintf(stderr, "morph=(0,1) wood->metal  RMS early/mid/late: %5.2f / %5.2f / %5.2f dBFS\n",
            segRmsDb(a, 0, earlyEnd), segRmsDb(a, midStart, midEnd), segRmsDb(a, lateStart, lateEnd));
        std::fprintf(stderr, "morph=(1,0) metal->wood  RMS early/mid/late: %5.2f / %5.2f / %5.2f dBFS\n",
            segRmsDb(b, 0, earlyEnd), segRmsDb(b, midStart, midEnd), segRmsDb(b, lateStart, lateEnd));
        std::fprintf(stderr, "RMS difference  early / mid / late: %5.2f / %5.2f / %5.2f dBFS\n",
            rmsDiffDb({a.begin(), a.begin() + earlyEnd},
                      {b.begin(), b.begin() + earlyEnd}),
            rmsDiffDb({a.begin() + midStart, a.begin() + midEnd},
                      {b.begin() + midStart, b.begin() + midEnd}),
            rmsDiffDb({a.begin() + lateStart, a.begin() + lateEnd},
                      {b.begin() + lateStart, b.begin() + lateEnd}));
    }

    // Dump WAVs for manual A/B (defaults config).
    auto a = renderHit(0.0f, 1.0f, 0.35f, 0.5f);
    auto b = renderHit(1.0f, 0.0f, 0.35f, 0.5f);
    writeWav("F:/tmp/morphA_woodToMetal.wav", a);
    writeWav("F:/tmp/morphB_metalToWood.wav", b);
    std::fprintf(stderr, "\nWrote F:/tmp/morphA_woodToMetal.wav and morphB_metalToWood.wav\n");
}
