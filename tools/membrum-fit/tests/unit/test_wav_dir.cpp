// loadKitJson smoke: relative WAV paths resolve against the JSON's parent.
#include "src/ingestion/wav_dir.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

TEST_CASE("loadKitJson: maps MIDI note -> resolved WAV path") {
    const auto dir = std::filesystem::temp_directory_path() / "membrum_fit_wav_dir";
    std::filesystem::create_directories(dir);
    const auto json = dir / "kit.json";
    {
        std::ofstream f(json);
        f << R"({"36":"kick.wav","38":"snare.wav","42":"hat.wav","12":"out_of_range.wav"})";
    }
    const auto spec = MembrumFit::Ingestion::loadKitJson(json);
    REQUIRE(spec.midiNoteToFile.size() == 3);  // 12 is out of [36, 67]
    REQUIRE(spec.midiNoteToFile.at(36).filename() == "kick.wav");
    REQUIRE(spec.midiNoteToFile.at(36).parent_path() == dir);
    REQUIRE(spec.midiNoteToFile.at(38).filename() == "snare.wav");
    REQUIRE(spec.midiNoteToFile.at(42).filename() == "hat.wav");

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
