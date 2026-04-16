// Per-pad JSON writer round-trip.
#include "src/preset_io/json_writer.h"

#include "dsp/default_kit.h"

#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <filesystem>
#include <fstream>

TEST_CASE("Per-pad JSON writer: round-trip preserves PadConfig fields") {
    Membrum::PadConfig p{};
    Membrum::DefaultKit::applyTemplate(p, Membrum::DrumTemplate::Snare);
    p.size = 0.42f;

    const auto path = std::filesystem::temp_directory_path() / "membrum_fit_pad.json";
    REQUIRE(MembrumFit::PresetIO::writePadJson(path, p, "TestPad"));

    std::ifstream f(path);
    nlohmann::json j;
    f >> j;
    REQUIRE(j["format_version"] == 1);
    REQUIRE(j["name"] == "TestPad");
    REQUIRE(j["bodyModel"].get<int>() == static_cast<int>(p.bodyModel));
    REQUIRE(j["material"].get<float>() == Catch::Approx(p.material));
    REQUIRE(j["size"].get<float>()     == Catch::Approx(0.42f));
    REQUIRE(j["macros"]["punch"].get<float>() == Catch::Approx(0.5f));

    std::error_code ec;
    std::filesystem::remove(path, ec);
}
