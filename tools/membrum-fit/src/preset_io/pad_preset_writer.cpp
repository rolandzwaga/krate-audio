#include "pad_preset_writer.h"

#include "preset/membrum_preset_container.h"
#include "state/state_codec.h"

#include "pluginterfaces/base/ibstream.h"
#include "public.sdk/source/common/memorystream.h"

namespace MembrumFit::PresetIO {

bool writePadPreset(const std::filesystem::path& outputPath,
                    const Membrum::PadConfig& pad,
                    const std::string& presetName,
                    const std::string& subcategory) {
    Membrum::State::PadPresetSnapshot snap = Membrum::State::toPadPresetSnapshot(pad);
    Steinberg::MemoryStream stream;
    if (Membrum::State::writePadPresetBlob(&stream, snap) != Steinberg::kResultOk) {
        return false;
    }
    return Membrum::Preset::writePresetFile(outputPath, &stream, presetName, subcategory);
}

}  // namespace MembrumFit::PresetIO
