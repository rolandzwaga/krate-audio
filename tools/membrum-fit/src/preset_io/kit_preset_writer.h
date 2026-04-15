#pragma once

#include "dsp/pad_config.h"

#include <array>
#include <filesystem>
#include <string>

namespace MembrumFit::PresetIO {

// Write a Membrum kit preset (v6 .vstpreset). Globals and macros default to
// neutral values because offline fits can't infer coupling from isolated
// samples (spec §9 risk #7 & #8). See specs/141-membrum-phase6-ui/data-model.md §9.
bool writeKitPreset(const std::filesystem::path& outputPath,
                    const std::array<Membrum::PadConfig, 32>& pads,
                    const std::string& presetName,
                    const std::string& subcategory = "Acoustic");

}  // namespace MembrumFit::PresetIO
