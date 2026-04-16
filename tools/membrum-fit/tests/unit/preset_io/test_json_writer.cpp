// JSON writer smoke: write a kit, parse it back, sanity-check the schema
// matches spec 141 §10.
#include "src/preset_io/json_writer.h"

#include "dsp/default_kit.h"

#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <array>
#include <filesystem>
#include <fstream>

TEST_CASE("Kit JSON writer: produces a 32-pad object matching spec 141 §10") {
    std::array<Membrum::PadConfig, 32> pads{};
    Membrum::DefaultKit::applyTemplate(pads[0], Membrum::DrumTemplate::Kick);
    Membrum::DefaultKit::applyTemplate(pads[2], Membrum::DrumTemplate::Snare);
    for (std::size_t i = 1; i < pads.size(); i += 2)
        Membrum::DefaultKit::applyTemplate(pads[i], Membrum::DrumTemplate::Perc);

    const auto path = std::filesystem::temp_directory_path() / "membrum_fit_kit.json";
    REQUIRE(MembrumFit::PresetIO::writeKitJson(path, pads, "TestKit"));

    std::ifstream f(path);
    nlohmann::json j;
    f >> j;
    REQUIRE(j["format_version"] == 6);
    REQUIRE(j["name"] == "TestKit");
    REQUIRE(j["pads"].size() == 32);
    REQUIRE(j["pads"][0]["material"].get<float>() == Catch::Approx(pads[0].material));
    REQUIRE(j["pads"][0]["macros"]["tightness"].get<float>() == Catch::Approx(0.5f));
    REQUIRE(j["pads"][2]["bodyModel"].get<int>() == static_cast<int>(pads[2].bodyModel));

    std::error_code ec;
    std::filesystem::remove(path, ec);
}
