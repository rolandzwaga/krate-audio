// SFZ ingest: note-name keys (c3, F#2, Bb4) parse to MIDI note numbers.
#include "src/ingestion/sfz_ingest.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

TEST_CASE("SFZ: note-name keys parse to MIDI numbers") {
    const auto dir = std::filesystem::temp_directory_path() / "membrum_fit_sfz_names";
    std::filesystem::create_directories(dir);
    const auto sfz = dir / "kit.sfz";
    {
        std::ofstream f(sfz);
        f << "<region> key=c2 sample=kick.wav\n";          // C2 = MIDI 36
        f << "<region> key=d2 sample=snare.wav\n";         // D2 = MIDI 38
        f << "<region> key=f#2 sample=hat_closed.wav\n";   // F#2 = MIDI 42
    }
    const auto spec = MembrumFit::Ingestion::loadKitSFZ(sfz);
    REQUIRE(spec.midiNoteToFile.size() == 3);
    REQUIRE(spec.midiNoteToFile.at(36).filename() == "kick.wav");
    REQUIRE(spec.midiNoteToFile.at(38).filename() == "snare.wav");
    REQUIRE(spec.midiNoteToFile.at(42).filename() == "hat_closed.wav");

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
