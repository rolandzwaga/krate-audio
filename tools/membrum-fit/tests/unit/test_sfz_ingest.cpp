// Minimal SFZ parser smoke.
#include "src/ingestion/sfz_ingest.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

TEST_CASE("SFZ ingest: maps key=N sample=foo.wav to MIDI note") {
    const auto dir = std::filesystem::temp_directory_path() / "membrum_fit_sfz";
    std::filesystem::create_directories(dir);
    const auto sfz = dir / "kit.sfz";
    {
        std::ofstream f(sfz);
        f << "<group>\n";
        f << "<region> key=36 sample=kick.wav\n";
        f << "<region> key=38 sample=snare.wav lovel=0 hivel=100\n";
        f << "<region> key=38 sample=snare_hard.wav lovel=101 hivel=127\n";
        f << "<region> key=42 sample=hat.wav\n";
    }
    const auto spec = MembrumFit::Ingestion::loadKitSFZ(sfz);
    REQUIRE(spec.midiNoteToFile.size() == 3);
    REQUIRE(spec.midiNoteToFile.at(36).filename() == "kick.wav");
    REQUIRE(spec.midiNoteToFile.at(38).filename() == "snare_hard.wav");  // higher velocity picks
    REQUIRE(spec.midiNoteToFile.at(42).filename() == "hat.wav");

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
