#pragma once

#include "types.h"

#include <filesystem>
#include <optional>

namespace MembrumFit {

// Load a WAV file from disk, mix to mono, resample to targetSampleRate,
// normalise peak to -1 dBFS. Returns std::nullopt on read failure.
//
// dr_wav handles int16/int24/int32 PCM, float32, 44.1-192 kHz.
// Stereo inputs are mid-summed; interChannelCorrelation < 0.7 is recorded as
// a warning in the returned sample (see SegmentedSample's warning path).
std::optional<LoadedSample> loadSample(const std::filesystem::path& wav,
                                       double targetSampleRate = 44100.0);

}  // namespace MembrumFit
