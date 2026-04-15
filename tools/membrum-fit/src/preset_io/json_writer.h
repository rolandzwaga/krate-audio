#pragma once

#include "dsp/pad_config.h"

#include <array>
#include <filesystem>
#include <string>

namespace MembrumFit::PresetIO {

// Write a JSON intermediate matching specs/141-membrum-phase6-ui/data-model.md §10.
bool writeKitJson(const std::filesystem::path& outputPath,
                  const std::array<Membrum::PadConfig, 32>& pads,
                  const std::string& presetName);

bool writePadJson(const std::filesystem::path& outputPath,
                  const Membrum::PadConfig& pad,
                  const std::string& presetName);

}  // namespace MembrumFit::PresetIO
