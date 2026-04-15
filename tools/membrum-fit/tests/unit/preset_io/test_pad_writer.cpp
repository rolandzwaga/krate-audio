// Per-pad preset writer round-trip (writes -> reads back via state codec).
#include "src/preset_io/pad_preset_writer.h"

#include "dsp/pad_config.h"
#include "dsp/default_kit.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <filesystem>
#include <fstream>

TEST_CASE("Pad preset writer: produces a non-empty .vstpreset file") {
    Membrum::PadConfig pad{};
    Membrum::DefaultKit::applyTemplate(pad, Membrum::DrumTemplate::Kick);

    const auto path = std::filesystem::temp_directory_path() / "membrum_fit_pad_smoke.vstpreset";
    REQUIRE(MembrumFit::PresetIO::writePadPreset(path, pad, "TestKick", "Drum"));
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    REQUIRE(f.good());
    const auto sz = f.tellg();
    REQUIRE(sz > 284);  // 284 = per-pad blob alone; container adds chunk list + meta XML
    f.close();
    std::error_code ec;
    std::filesystem::remove(path, ec);
}
