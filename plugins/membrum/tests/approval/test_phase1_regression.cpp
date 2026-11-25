// ==============================================================================
// Default-patch output regression test (Phase 2 -- T010, T025)
// ==============================================================================
// The default patch (default PadConfig on pad 0: Impulse + Membrane, Tone
// Shaper + Unnatural Zone bypassed) must render the committed golden. This
// guards against unintended changes to the default signal path.
//
// The render goes through Membrum::VoicePool in 64-sample blocks, the path a
// host runs (fix plan T2.1, MB-142), and is compared by render fingerprint
// (tests/test_helpers/render_fingerprint.h) at the shared cross-toolchain
// tolerances, not by raw samples. A missing golden fails the test.
//
// The golden binary lives at plugins/membrum/tests/golden/phase1_default.bin
// and is regenerated only by explicitly running the hidden case
//     membrum_tests.exe "Generate Phase 1 golden reference"
// Every re-baseline is a reviewed decision: append a line saying why to
// plugins/membrum/tests/golden/phase1_default.md.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "dsp/pad_config.h"
#include "voice_pool/voice_pool.h"

#include "render_fingerprint.h"

#include <krate/dsp/core/db_utils.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <vector>

namespace {

constexpr double       kSampleRate      = 44100.0;
constexpr int          kBlockSize       = 64;
constexpr int          kLengthSamples   = static_cast<int>(kSampleRate * 0.5); // 500 ms
constexpr std::uint8_t kNote            = 36;   // pad 0
constexpr float        kTriggerVelocity = 0.8f;

// Left channel of the default pad: the default pan 0.5 is unity on both sides.
std::vector<float> renderPhase1DefaultPatch()
{
    auto pool = std::make_unique<Membrum::VoicePool>();
    pool->padConfigMut(0) = Membrum::PadConfig{};
    pool->prepare(kSampleRate, kBlockSize);

    pool->noteOn(kNote, kTriggerVelocity);

    std::vector<float> outL(static_cast<std::size_t>(kLengthSamples), 0.0f);
    std::vector<float> outR(static_cast<std::size_t>(kLengthSamples), 0.0f);
    for (int pos = 0; pos < kLengthSamples; pos += kBlockSize)
    {
        const int n = std::min(kBlockSize, kLengthSamples - pos);
        pool->processBlock(outL.data() + pos, outR.data() + pos, n);
    }
    return outL;
}

std::filesystem::path goldenPath()
{
    return std::filesystem::path(MEMBRUM_RESOURCES_DIR) / ".." / "tests" / "golden"
           / "phase1_default.bin";
}

bool writeGolden(const std::filesystem::path& path, const std::vector<float>& samples)
{
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

bool readGolden(const std::filesystem::path& path, std::vector<float>& samples)
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
// ==============================================================================
TEST_CASE("Generate Phase 1 golden reference", "[.generate_golden]")
{
    const auto samples = renderPhase1DefaultPatch();
    REQUIRE(static_cast<int>(samples.size()) == kLengthSamples);
    for (float s : samples)
        REQUIRE(Krate::DSP::detail::isFinite(s));

    const auto path = goldenPath();
    INFO("golden: " << path.string());
    REQUIRE(writeGolden(path, samples));
}

// ==============================================================================
// Regression test (SC-005 / FR-095): the default patch, rendered through the
// voice pool, matches the golden's fingerprint.
// ==============================================================================
TEST_CASE("Phase 2 default patch matches Phase 1 golden reference (render fingerprint)",
          "[membrum][regression][phase1]")
{
    const auto current = renderPhase1DefaultPatch();
    REQUIRE(static_cast<int>(current.size()) == kLengthSamples);

    std::vector<float> golden;
    const auto path = goldenPath();
    INFO("golden: " << path.string());
    REQUIRE(readGolden(path, golden));
    REQUIRE(golden.size() == current.size());

    using Krate::DSP::TestUtils::compareFingerprints;
    using Krate::DSP::TestUtils::fingerprintRender;
    const auto cmp = compareFingerprints(fingerprintRender(std::span<const float>(current)),
                                         fingerprintRender(std::span<const float>(golden)));
    INFO(cmp.detail);
    INFO("worst metric rel err " << cmp.worstMetricRelativeError
         << ", worst checkpoint err " << cmp.worstSampleError);
    CHECK(cmp.withinTolerance());
}
