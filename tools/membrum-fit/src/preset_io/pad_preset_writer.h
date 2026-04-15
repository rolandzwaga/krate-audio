#pragma once

#include "dsp/pad_config.h"

#include <filesystem>
#include <string>

namespace MembrumFit::PresetIO {

// Write a Membrum per-pad preset (.vstpreset, v1 284-byte blob inside the
// VST3 container). See specs/139-membrum-phase4-pads/data-model.md §7.
bool writePadPreset(const std::filesystem::path& outputPath,
                    const Membrum::PadConfig& pad,
                    const std::string& presetName,
                    const std::string& subcategory = "Drum");

}  // namespace MembrumFit::PresetIO
