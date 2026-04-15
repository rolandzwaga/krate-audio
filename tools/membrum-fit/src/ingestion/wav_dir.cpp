#include "wav_dir.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>
#include <string>

namespace MembrumFit::Ingestion {

KitSpec loadKitJson(const std::filesystem::path& jsonPath) {
    KitSpec spec;
    spec.rootDir = jsonPath.parent_path();
    std::ifstream f(jsonPath);
    if (!f) throw std::runtime_error("cannot open " + jsonPath.string());
    nlohmann::json j;
    f >> j;
    if (!j.is_object()) throw std::runtime_error("kit JSON must be an object of midiNote -> wav");
    for (auto it = j.begin(); it != j.end(); ++it) {
        try {
            const int note = std::stoi(it.key());
            if (note < 36 || note > 67) continue;
            const std::string name = it.value().get<std::string>();
            std::filesystem::path p = name;
            if (p.is_relative()) p = spec.rootDir / p;
            spec.midiNoteToFile[note] = p;
        } catch (...) {}
    }
    return spec;
}

}  // namespace MembrumFit::Ingestion
