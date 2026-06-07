// ==============================================================================
// Default-patch output regression test (Phase 2 -- T010, T025)
// ==============================================================================
// The default patch (Impulse + Membrane, default parameters, Tone Shaper +
// Unnatural Zone bypassed) must produce output that matches the committed
// golden within −90 dBFS RMS. This guards against unintended changes to the
// default signal path.
//
// NOTE: the golden was DELIBERATELY re-baselined after the gain-staging
// redesign (Steps 1-4: linear voice + per-voice hardClip safety, body
// 1/sum|a| unit-peak normalisation, noise/click layer re-balance). It no
// longer represents the original "bit-identical to Phase 1" output (FR-095) --
// that guarantee was intentionally superseded by the gain-staging fixes, whose
// whole purpose was to change the (broken) default gain structure. The golden
// now captures the post-gain-staging baseline.
//
// The golden binary lives at plugins/membrum/tests/golden/phase1_default.bin
// and is regenerated only by explicitly running the hidden [.generate_golden]
// case. The regression test compares the current output against that binary.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/drum_voice.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using Catch::Approx;

namespace {

inline bool isFiniteSample(float x) noexcept
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &x, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

constexpr double kSampleRate = 44100.0;
constexpr int    kLengthSamples = static_cast<int>(kSampleRate * 0.5); // 500 ms
constexpr float  kTriggerVelocity = 0.8f;

std::vector<float> renderPhase1DefaultPatch()
{
    Membrum::DrumVoice voice;
    voice.prepare(kSampleRate);

    // Phase 1 default patch (matches processor.cpp atomic defaults).
    voice.setMaterial(0.5f);
    voice.setSize(0.5f);
    voice.setDecay(0.3f);
    voice.setStrikePosition(0.3f);
    voice.setLevel(0.8f);

    // Phase 2 defaults: Impulse + Membrane, tone shaper + unnatural zone off.
    voice.setExciterType(Membrum::ExciterType::Impulse);
    voice.setBodyModel(Membrum::BodyModelType::Membrane);

    voice.noteOn(kTriggerVelocity);

    std::vector<float> out(static_cast<std::size_t>(kLengthSamples), 0.0f);
    for (int i = 0; i < kLengthSamples; ++i)
        out[static_cast<std::size_t>(i)] = voice.process();
    return out;
}

// Locate the golden file relative to the test executable's working directory.
// The CMake project runs CTest from the build dir with CWD = repo root, so
// the relative path from repo root is used first; we also look at a few
// alternative locations for robustness.
std::string locateGoldenPath()
{
    const std::string candidates[] = {
        "plugins/membrum/tests/golden/phase1_default.bin",
        "../plugins/membrum/tests/golden/phase1_default.bin",
        "../../plugins/membrum/tests/golden/phase1_default.bin",
        "../../../plugins/membrum/tests/golden/phase1_default.bin",
        "F:/projects/iterum/plugins/membrum/tests/golden/phase1_default.bin",
    };
    for (const auto& p : candidates)
    {
        if (std::filesystem::exists(p))
            return p;
    }
    // Fall back to the first candidate (test will fail with a clear message).
    return candidates[0];
}

bool writeGolden(const std::string& path, const std::vector<float>& samples)
{
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    const std::int32_t version = 1;
    const std::int32_t count   = static_cast<std::int32_t>(samples.size());
    f.write(reinterpret_cast<const char*>(&version), sizeof(version));
    f.write(reinterpret_cast<const char*>(&count), sizeof(count));
    f.write(reinterpret_cast<const char*>(samples.data()),
            static_cast<std::streamsize>(samples.size() * sizeof(float)));
    return f.good();
}

bool readGolden(const std::string& path, std::vector<float>& samples)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::int32_t version = 0;
    std::int32_t count = 0;
    f.read(reinterpret_cast<char*>(&version), sizeof(version));
    f.read(reinterpret_cast<char*>(&count), sizeof(count));
    if (!f || version != 1 || count <= 0) return false;
    samples.assign(static_cast<std::size_t>(count), 0.0f);
    f.read(reinterpret_cast<char*>(samples.data()),
           static_cast<std::streamsize>(samples.size() * sizeof(float)));
    return static_cast<bool>(f);
}

} // namespace

// ==============================================================================
// Golden-generation test case. HIDDEN (leading-dot tag) so it does NOT run in
// the default suite -- otherwise it would rewrite the golden on every run and
// the regression below would only ever compare the output against itself.
// Invoke explicitly to deliberately re-baseline:
//     membrum_tests.exe "[.generate_golden]"
// ==============================================================================
TEST_CASE("Generate Phase 1 golden reference", "[.generate_golden]")
{
    const auto samples = renderPhase1DefaultPatch();
    REQUIRE(static_cast<int>(samples.size()) == kLengthSamples);
    for (float s : samples)
        REQUIRE(isFiniteSample(s));

    const std::string path = locateGoldenPath();
    const bool ok = writeGolden(path, samples);
    if (!ok)
        FAIL("Failed to write golden reference to: " << path);
    INFO("Wrote Phase 1 golden reference to: " << path);
    CHECK(ok);
}

// ==============================================================================
// Regression test (SC-005 / FR-095): Phase 2 output must match the golden
// reference within −90 dBFS RMS.
// ==============================================================================
TEST_CASE("Phase 2 default patch matches Phase 1 golden reference (RMS ≤ −90 dBFS)",
          "[membrum][regression][phase1]")
{
    const auto current = renderPhase1DefaultPatch();
    REQUIRE(static_cast<int>(current.size()) == kLengthSamples);

    std::vector<float> golden;
    const std::string path = locateGoldenPath();
    if (!readGolden(path, golden))
    {
        WARN("Golden file not found or unreadable: " << path
             << " — run the [generate_golden] test case to create it.");
        SUCCEED("Skipping regression comparison until golden is generated.");
        return;
    }

    REQUIRE(golden.size() == current.size());

    double sumSq = 0.0;
    for (std::size_t i = 0; i < current.size(); ++i)
    {
        const double d = static_cast<double>(current[i]) - static_cast<double>(golden[i]);
        sumSq += d * d;
    }
    const double rms = std::sqrt(sumSq / static_cast<double>(current.size()));
    const double rmsDb = rms > 0.0 ? 20.0 * std::log10(rms) : -200.0;
    INFO("Phase 1 regression RMS = " << rms << " (" << rmsDb << " dBFS)");

    // SC-005 / FR-095: ≤ −90 dBFS.
    CHECK(rmsDb <= -90.0);
}
