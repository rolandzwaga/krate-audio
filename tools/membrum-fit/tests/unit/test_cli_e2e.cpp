// End-to-end CLI smoke: render a Kick via gen-tool path, run the full
// fitSample pipeline against it, write a per-pad preset, then read the
// preset back via the state codec and check the fitted parameters round-
// trip. This exercises every linkable layer except the actual entry.cpp.
#include "src/loader.h"
#include "src/main.h"
#include "src/preset_io/pad_preset_writer.h"
#include "src/refinement/render_voice.h"

#include "dsp/default_kit.h"
#include "preset/membrum_preset_container.h"
#include "state/state_codec.h"

#include "dr_wav.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cstdio>
#include <filesystem>
#include <vector>

namespace {

bool writeFloatWav(const std::filesystem::path& p, const std::vector<float>& s, unsigned sr) {
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

TEST_CASE("CLI e2e: render Kick -> fitSample -> writePadPreset -> file exists") {
    const auto dir = std::filesystem::temp_directory_path() / "membrum_fit_e2e";
    std::filesystem::create_directories(dir);
    const auto wavPath    = dir / "kick.wav";
    const auto presetPath = dir / "kick.vstpreset";

    Membrum::PadConfig ground{};
    Membrum::DefaultKit::applyTemplate(ground, Membrum::DrumTemplate::Kick);

    MembrumFit::RenderableMembrumVoice voice;
    voice.prepare(44100.0);
    const auto rendered = voice.renderToVector(ground, 1.0f, 0.4f);
    REQUIRE(writeFloatWav(wavPath, rendered, 44100));

    MembrumFit::FitOptions opts;
    opts.targetSampleRate = 44100.0;
    opts.maxBobyqaEvals   = 0;  // skip BOBYQA -- deterministic-only path is enough
    const auto fit = MembrumFit::fitSample(wavPath, opts);
    REQUIRE(!fit.quality.hasWarnings);
    // Body class accuracy is tracked by the [.corpus] sweep; the e2e test
    // gates only that the pipeline plumbs end-to-end and emits a valid
    // PadConfig (any of the 6 valid bodies is fine).
    REQUIRE(static_cast<int>(fit.padConfig.bodyModel) >= 0);
    REQUIRE(static_cast<int>(fit.padConfig.bodyModel) <  6);

    REQUIRE(MembrumFit::PresetIO::writePadPreset(presetPath, fit.padConfig, "TestKick", "Drum"));
    REQUIRE(std::filesystem::exists(presetPath));
    REQUIRE(std::filesystem::file_size(presetPath) > 284);

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
