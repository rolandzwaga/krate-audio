#pragma once

#include <filesystem>
#include <map>
#include <string>

namespace MembrumFit::Ingestion {

// WAV-directory kit spec: maps MIDI note -> WAV path.
struct KitSpec {
    std::filesystem::path rootDir;                          // sample dir
    std::map<int, std::filesystem::path> midiNoteToFile;    // 36..67
};

// Read a JSON file of the form { "36": "kick.wav", "38": "snare.wav", ... }.
// Relative WAV paths are resolved against the JSON file's parent directory.
KitSpec loadKitJson(const std::filesystem::path& jsonPath);

}  // namespace MembrumFit::Ingestion
