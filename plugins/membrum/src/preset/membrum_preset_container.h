#pragma once

// ==============================================================================
// Membrum VST3 preset container writer
// ==============================================================================
// Writes a Membrum state blob (as produced by Membrum::State::writeKitBlob or
// writePadPresetBlob) to a .vstpreset file with the standard VST3 header,
// chunk list, and metadata. Used by the offline tools/membrum-fit tool and any
// other code path that needs to emit a load-ready preset without a live
// IComponent instance.
// ==============================================================================

#include "pluginterfaces/base/ibstream.h"

#include <filesystem>
#include <string>

namespace Membrum::Preset {

// Write a .vstpreset file wrapping the given component-state IBStream.
// The stream MUST already contain the full Membrum kit or pad-preset blob
// (produced by the state codec). The stream's read position is reset to 0
// before wrapping; on return the stream is left seeked at end.
//
// Parameters:
//   outputPath   - absolute or relative path; parent directory must exist
//   componentState - IBStream with the kit/pad-preset blob (not released)
//   presetName   - human-readable name written to the MetaInfo chunk
//   subcategory  - VST3 musical subcategory (e.g. "Drum|Kick")
//   description  - optional free-form comment
//
// Returns true on success, false on any I/O or SDK failure.
bool writePresetFile(const std::filesystem::path& outputPath,
                     Steinberg::IBStream* componentState,
                     const std::string& presetName,
                     const std::string& subcategory,
                     const std::string& description = {});

}  // namespace Membrum::Preset
