// Loader smoke test -- writes a small sine WAV via dr_wav and reads it back.
#include "src/loader.h"

#include "dr_wav.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <filesystem>
#include <vector>

TEST_CASE("Loader: round-trips a synthetic mono WAV") {
    const auto path = std::filesystem::temp_directory_path() / "membrum_fit_loader_smoke.wav";
    constexpr unsigned sr = 44100;
    constexpr unsigned len = 4096;
    std::vector<float> sine(len);
    for (unsigned i = 0; i < len; ++i)
        sine[i] = 0.5f * std::sin(2.0f * 3.14159265358979323846f * 440.0f * i / sr);

    drwav_data_format fmt{};
    fmt.container = drwav_container_riff;
    fmt.format    = DR_WAVE_FORMAT_IEEE_FLOAT;
    fmt.channels  = 1;
    fmt.sampleRate = sr;
    fmt.bitsPerSample = 32;
    drwav w;
    REQUIRE(drwav_init_file_write(&w, path.string().c_str(), &fmt, nullptr));
    REQUIRE(drwav_write_pcm_frames(&w, len, sine.data()) == len);
    drwav_uninit(&w);

    auto loaded = MembrumFit::loadSample(path, 44100.0);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->samples.size() == len);
    REQUIRE(loaded->sampleRate == Catch::Approx(44100.0));
    REQUIRE(loaded->channelCorrelation == Catch::Approx(1.0f));
    // Peak should be normalised to ~ -1 dBFS = 0.891 amplitude.
    float peak = 0.0f;
    for (float x : loaded->samples) peak = std::max(peak, std::abs(x));
    REQUIRE(peak == Catch::Approx(0.891f).margin(0.01f));

    std::error_code ec;
    std::filesystem::remove(path, ec);
}
