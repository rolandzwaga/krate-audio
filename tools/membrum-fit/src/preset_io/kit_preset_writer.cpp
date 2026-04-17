#include "kit_preset_writer.h"

#include "preset/membrum_preset_container.h"
#include "state/state_codec.h"

#include "pluginterfaces/base/ibstream.h"
#include "public.sdk/source/common/memorystream.h"

namespace MembrumFit::PresetIO {

bool writeKitPreset(const std::filesystem::path& outputPath,
                    const std::array<Membrum::PadConfig, 32>& pads,
                    const std::string& presetName,
                    const std::string& subcategory) {
    Membrum::State::KitSnapshot kit{};
    kit.maxPolyphony = 8;
    kit.voiceStealingPolicy = 0;
    for (std::size_t i = 0; i < pads.size(); ++i) {
        kit.pads[i] = Membrum::State::toPadSnapshot(pads[i]);
    }
    // Sympathetic coupling defaults: spec §9 risk #7 recommended zero because
    // coupling can't be inferred from isolated samples. But zero makes fitted
    // kits sound sterile (no snare buzz under kicks, no tom ring). Real drum
    // kits always have these interactions. Use moderate defaults — user can
    // still dial them to taste in the plugin.
    kit.globalCoupling = 0.35;
    kit.snareBuzz      = 0.4;
    kit.tomResonance   = 0.3;
    kit.couplingDelayMs = 1.0;
    kit.selectedPadIndex = 0;
    kit.hasSession = false;

    Steinberg::MemoryStream stream;
    if (Membrum::State::writeKitBlob(&stream, kit) != Steinberg::kResultOk) {
        return false;
    }
    return Membrum::Preset::writePresetFile(outputPath, &stream, presetName, subcategory);
}

}  // namespace MembrumFit::PresetIO
