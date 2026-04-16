// Loader: stereo correlation + resample paths.
#include "src/loader.h"

#include "dr_wav.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <filesystem>
#include <vector>

namespace {

bool writeStereoWav(const std::filesystem::path& p, unsigned sr,
                    const std::vector<float>& l, const std::vector<float>& r) {
    drwav_data_format fmt{};
    fmt.container = drwav_container_riff;
    fmt.format = DR_WAVE_FORMAT_IEEE_FLOAT;
    fmt.channels = 2;
    fmt.sampleRate = sr;
    fmt.bitsPerSample = 32;
    drwav w;
    if (!drwav_init_file_write(&w, p.string().c_str(), &fmt, nullptr)) return false;
    std::vector<float> interleaved(l.size() * 2);
    for (std::size_t i = 0; i < l.size(); ++i) {
        interleaved[i * 2 + 0] = l[i];
        interleaved[i * 2 + 1] = r[i];
    }
    const auto wrote = drwav_write_pcm_frames(&w, l.size(), interleaved.data());
    drwav_uninit(&w);
    return wrote == l.size();
}

bool writeMonoWav(const std::filesystem::path& p, unsigned sr, const std::vector<float>& s) {
    drwav_data_format fmt{};
    fmt.container = drwav_container_riff;
    fmt.format = DR_WAVE_FORMAT_IEEE_FLOAT;
    fmt.channels = 1;
    fmt.sampleRate = sr;
    fmt.bitsPerSample = 32;
    drwav w;
    if (!drwav_init_file_write(&w, p.string().c_str(), &fmt, nullptr)) return false;
    const auto wrote = drwav_write_pcm_frames(&w, s.size(), s.data());
    drwav_uninit(&w);
    return wrote == s.size();
}

}  // namespace

TEST_CASE("Loader: high-correlation stereo (~ mono) reports correlation > 0.95") {
    constexpr unsigned sr = 44100;
    constexpr int N = 4096;
    constexpr float pi = 3.14159265358979323846f;
    std::vector<float> l(N), r(N);
    for (int i = 0; i < N; ++i) {
        const float v = 0.4f * std::sin(2.0f * pi * 440.0f * i / sr);
        l[i] = v; r[i] = v;
    }
    const auto path = std::filesystem::temp_directory_path() / "membrum_fit_stereo_high.wav";
    REQUIRE(writeStereoWav(path, sr, l, r));
    auto loaded = MembrumFit::loadSample(path, 44100.0);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->channelCorrelation > 0.95f);
    std::error_code ec; std::filesystem::remove(path, ec);
}

TEST_CASE("Loader: low-correlation stereo flags warning candidate") {
    constexpr unsigned sr = 44100;
    constexpr int N = 4096;
    constexpr float pi = 3.14159265358979323846f;
    std::vector<float> l(N), r(N);
    for (int i = 0; i < N; ++i) {
        l[i] = 0.4f * std::sin(2.0f * pi * 440.0f * i / sr);
        r[i] = 0.4f * std::sin(2.0f * pi * 440.0f * i / sr + pi);  // 180° out
    }
    const auto path = std::filesystem::temp_directory_path() / "membrum_fit_stereo_low.wav";
    REQUIRE(writeStereoWav(path, sr, l, r));
    auto loaded = MembrumFit::loadSample(path, 44100.0);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->channelCorrelation < -0.95f);
    std::error_code ec; std::filesystem::remove(path, ec);
}

TEST_CASE("Loader: 48 kHz mono resamples to 44.1 kHz target") {
    constexpr unsigned srIn = 48000;
    constexpr int N = static_cast<int>(srIn * 0.1);
    std::vector<float> sig(N);
    constexpr float pi = 3.14159265358979323846f;
    for (int i = 0; i < N; ++i) sig[i] = 0.5f * std::sin(2.0f * pi * 220.0f * i / srIn);
    const auto path = std::filesystem::temp_directory_path() / "membrum_fit_48k_mono.wav";
    REQUIRE(writeMonoWav(path, srIn, sig));
    auto loaded = MembrumFit::loadSample(path, 44100.0);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->sampleRate == Catch::Approx(44100.0));
    // Length scales by 44100/48000 ~ 0.91875.
    REQUIRE(static_cast<int>(loaded->samples.size())
            == Catch::Approx(static_cast<int>(N * 44100.0 / 48000.0)).margin(2));
    std::error_code ec; std::filesystem::remove(path, ec);
}
