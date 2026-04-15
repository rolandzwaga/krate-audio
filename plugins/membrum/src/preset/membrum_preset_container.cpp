#include "preset/membrum_preset_container.h"

#include "plugin_ids.h"

#include "public.sdk/source/vst/vstpresetfile.h"

#include <sstream>

namespace Membrum::Preset {

bool writePresetFile(const std::filesystem::path& outputPath,
                     Steinberg::IBStream* componentState,
                     const std::string& presetName,
                     const std::string& subcategory,
                     const std::string& description) {
    if (!componentState) {
        return false;
    }

    // Rewind component state so PresetFile can read it end-to-end.
    Steinberg::int64 ignored = 0;
    componentState->seek(0, Steinberg::IBStream::kIBSeekSet, &ignored);

    auto* fileStream = Steinberg::Vst::FileStream::open(outputPath.string().c_str(), "wb");
    if (!fileStream) {
        return false;
    }

    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xml << "<MetaInfo>\n";
    xml << "  <Attr id=\"MediaType\" value=\"VstPreset\" type=\"string\"/>\n";
    xml << "  <Attr id=\"PlugInName\" value=\"Membrum\" type=\"string\"/>\n";
    xml << "  <Attr id=\"PlugInCategory\" value=\"" << Membrum::kSubCategories << "\" type=\"string\"/>\n";
    xml << "  <Attr id=\"Name\" value=\"" << presetName << "\" type=\"string\"/>\n";
    xml << "  <Attr id=\"MusicalCategory\" value=\"" << subcategory << "\" type=\"string\"/>\n";
    xml << "  <Attr id=\"MusicalInstrument\" value=\"" << subcategory << "\" type=\"string\"/>\n";
    if (!description.empty()) {
        xml << "  <Attr id=\"Comment\" value=\"" << description << "\" type=\"string\"/>\n";
    }
    xml << "</MetaInfo>\n";
    const std::string xmlStr = xml.str();

    const bool ok = Steinberg::Vst::PresetFile::savePreset(
        fileStream,
        Membrum::kProcessorUID,
        componentState,
        nullptr,
        xmlStr.c_str(),
        static_cast<Steinberg::int32>(xmlStr.size()));

    fileStream->release();
    return ok;
}

}  // namespace Membrum::Preset
