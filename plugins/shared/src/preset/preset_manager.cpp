#include "preset_manager.h"
#include "../platform/preset_paths.h"

#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "public.sdk/source/vst/vstpresetfile.h"

#include <algorithm>
#include <cctype>
#include <ranges>
#include <sstream>
#include <utility>

namespace Krate::Plugins {

PresetManager::PresetManager(
    PresetManagerConfig config,
    Steinberg::Vst::IComponent* processor,
    Steinberg::Vst::IEditController* controller,
    std::filesystem::path userDirOverride,
    std::filesystem::path factoryDirOverride
)
    : config_(std::move(config))
    , processor_(processor)
    , controller_(controller)
    , userDirOverride_(std::move(userDirOverride))
    , factoryDirOverride_(std::move(factoryDirOverride))
{
}

PresetManager::~PresetManager() = default;

// =============================================================================
// Scanning
// =============================================================================

PresetManager::PresetList PresetManager::scanPresets() {
    cachedPresets_.clear();

    // Scan user presets
    auto userDir = getUserPresetDirectory();
    if (!userDir.empty() && std::filesystem::exists(userDir)) {
        scanDirectory(userDir, false);
    }

    // Scan factory presets
    auto factoryDir = getFactoryPresetDirectory();
    if (!factoryDir.empty() && std::filesystem::exists(factoryDir)) {
        scanDirectory(factoryDir, true);
    }

    // Sort by name
    std::sort(cachedPresets_.begin(), cachedPresets_.end());

    return cachedPresets_;
}

void PresetManager::scanDirectory(const std::filesystem::path& dir, bool isFactory) {
    namespace fs = std::filesystem;
    std::error_code ec;

    for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".vstpreset") {
            auto info = parsePresetFile(entry.path(), isFactory);
            if (info.isValid()) {
                cachedPresets_.push_back(std::move(info));
            }
        }
    }
}

PresetInfo PresetManager::parsePresetFile(const std::filesystem::path& path, bool isFactory) const {
    PresetInfo info;
    info.path = path;
    info.isFactory = isFactory;

    // Extract name from filename (without extension)
    info.name = path.stem().string();

    // Try to read metadata from preset file
    readMetadata(path, info);

    // Get parent directory for category and subcategory
    auto parent = path.parent_path();
    std::string parentName;
    if (parent.has_filename()) {
        parentName = parent.filename().string();
    }

    // If no category from metadata, use parent directory name
    if (info.category.empty()) {
        info.category = parentName;
    }

    // Derive subcategory from parent directory name
    // Check if parent directory matches any configured subcategory name
    for (const auto& subcatName : config_.subcategoryNames) {
        if (parentName == subcatName) {
            info.subcategory = subcatName;
            break;
        }
    }
    // If no match found, subcategory remains empty

    return info;
}

PresetManager::PresetList PresetManager::getPresetsForSubcategory(const std::string& subcategory) const {
    if (subcategory.empty()) {
        // Empty = "All" filter - return all presets
        return cachedPresets_;
    }

    PresetList filtered;
    for (const auto& preset : cachedPresets_) {
        if (preset.subcategory == subcategory) {
            filtered.push_back(preset);
        }
    }
    return filtered;
}

PresetManager::PresetList PresetManager::searchPresets(std::string_view query) const {
    if (query.empty()) {
        return cachedPresets_;
    }

    PresetList results;

    // Convert query to lowercase
    std::string lowerQuery(query);
    std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(),
        [](unsigned char c) { return std::tolower(c); });

    for (const auto& preset : cachedPresets_) {
        // Convert name to lowercase for comparison
        std::string lowerName = preset.name;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
            [](unsigned char c) { return std::tolower(c); });

        if (lowerName.find(lowerQuery) != std::string::npos) {
            results.push_back(preset);
        }
    }

    return results;
}

// =============================================================================
// Load/Save
// =============================================================================

bool PresetManager::loadPreset(const PresetInfo& preset) {
    if (!preset.isValid()) {
        lastError_ = "Invalid preset info";
        return false;
    }

    // Check if we can load via load provider (controller-only path)
    bool useLoadProvider = ((processor_ == nullptr) && loadProvider_);
    if (!processor_ && !loadProvider_) {
        lastError_ = "No component or load provider available";
        return false;
    }

    if (!std::filesystem::exists(preset.path)) {
        lastError_ = "Preset file not found: " + preset.path.string();
        return false;
    }

    // Open file stream for reading
    auto *stream = Steinberg::Vst::FileStream::open(preset.path.string().c_str(), "rb");
    if (!stream) {
        lastError_ = "Failed to open preset file: " + preset.path.string();
        return false;
    }

    bool success = false;

    if (useLoadProvider) {
        // Use PresetFile to parse the preset and extract component state
        Steinberg::Vst::PresetFile presetFile(stream);
        bool chunkListOk = presetFile.readChunkList();
        bool seekOk = chunkListOk && presetFile.seekToComponentState();

        if (seekOk) {
            // Get the entry for component state chunk
            const auto* entry = presetFile.getEntry(Steinberg::Vst::kComponentState);
            if (entry) {
                // Create a read-only stream for just the component state chunk
                auto componentStream = Steinberg::owned(
                    new Steinberg::Vst::ReadOnlyBStream(stream, entry->offset, entry->size));
                success = loadProvider_(componentStream);
                if (!success) {
                    lastError_ = "Load provider failed to apply preset state";
                }
            } else {
                lastError_ = "Preset file missing component state chunk";
            }
        } else if (!chunkListOk) {
            lastError_ = "Failed to read preset chunk list";
        } else {
            lastError_ = "Failed to seek to component state";
        }
    } else {
        // Standard VST3 loading with processor access
        success = Steinberg::Vst::PresetFile::loadPreset(
            stream,
            config_.processorUID,
            processor_,
            controller_
        );
    }

    stream->release();

    if (!success && lastError_.empty()) {
        lastError_ = "Failed to load preset data";
    }

    if (success) {
        lastError_.clear();
    }
    return success;
}

bool PresetManager::savePreset(
    const std::string& name,
    const std::string& subcategory,
    const std::string& description
) {
    if (!isValidPresetName(name)) {
        lastError_ = "Invalid preset name";
        return false;
    }

    // Check if we have a way to get the component state
    bool useStateProvider = ((processor_ == nullptr) && stateProvider_);
    if (!processor_ && !stateProvider_) {
        lastError_ = "No component or state provider available";
        return false;
    }

    // Create preset directory structure
    auto userDir = getUserPresetDirectory();
    if (userDir.empty()) {
        lastError_ = "Could not access user preset directory";
        return false;
    }

    // Determine subdirectory from subcategory
    std::string subDir = subcategory;
    if (subDir.empty() && !config_.subcategoryNames.empty()) {
        subDir = config_.subcategoryNames[0];
    }

    auto targetDir = userDir / subDir;
    Platform::ensureDirectoryExists(targetDir);

    // Create full path
    auto presetPath = targetDir / (name + ".vstpreset");

    // Build metadata XML using config
    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xml << "<MetaInfo>\n";
    xml << "  <Attr id=\"MediaType\" value=\"VstPreset\" type=\"string\"/>\n";
    xml << "  <Attr id=\"PlugInName\" value=\"" << config_.pluginName << "\" type=\"string\"/>\n";
    xml << "  <Attr id=\"PlugInCategory\" value=\"" << config_.pluginCategoryDesc << "\" type=\"string\"/>\n";
    xml << R"(  <Attr id="Name" value=")" << name << "\" type=\"string\"/>\n";
    xml << R"(  <Attr id="MusicalCategory" value=")" << subcategory << "\" type=\"string\"/>\n";
    xml << R"(  <Attr id="MusicalInstrument" value=")" << subDir << "\" type=\"string\"/>\n";
    if (!description.empty()) {
        xml << R"(  <Attr id="Comment" value=")" << description << "\" type=\"string\"/>\n";
    }
    xml << "</MetaInfo>\n";
    std::string xmlStr = xml.str();

    // Open file stream for writing
    auto *stream = Steinberg::Vst::FileStream::open(presetPath.string().c_str(), "wb");
    if (!stream) {
        lastError_ = "Failed to create preset file: " + presetPath.string();
        return false;
    }

    bool success = false;

    if (useStateProvider) {
        // Use state provider callback to get component state stream
        Steinberg::IBStream* componentStream = stateProvider_();
        if (!componentStream) {
            stream->release();
            lastError_ = "Failed to obtain component state";
            return false;
        }

        // Use stream-based savePreset overload
        success = Steinberg::Vst::PresetFile::savePreset(
            stream,
            config_.processorUID,
            componentStream,
            nullptr,  // No controller stream needed
            xmlStr.c_str(),
            static_cast<Steinberg::int32>(xmlStr.size())
        );

        componentStream->release();
    } else {
        // Use IComponent-based savePreset (original approach)
        success = Steinberg::Vst::PresetFile::savePreset(
            stream,
            config_.processorUID,
            processor_,
            controller_,
            xmlStr.c_str(),
            static_cast<Steinberg::int32>(xmlStr.size())
        );
    }

    stream->release();

    if (!success) {
        lastError_ = "Failed to save preset data";
        // Try to clean up failed file
        std::error_code ec;
        std::filesystem::remove(presetPath, ec);
        return false;
    }

    lastError_.clear();
    return true;
}

bool PresetManager::overwritePreset(const PresetInfo& preset) {
    if (preset.isFactory) {
        lastError_ = "Cannot overwrite factory presets";
        return false;
    }

    if (preset.path.empty() || !std::filesystem::exists(preset.path)) {
        lastError_ = "Preset file not found";
        return false;
    }

    // Check if we have a way to get the component state
    bool useStateProvider = ((processor_ == nullptr) && stateProvider_);
    if (!processor_ && !stateProvider_) {
        lastError_ = "No component or state provider available";
        return false;
    }

    // Build metadata XML (preserve existing metadata)
    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xml << "<MetaInfo>\n";
    xml << "  <Attr id=\"MediaType\" value=\"VstPreset\" type=\"string\"/>\n";
    xml << "  <Attr id=\"PlugInName\" value=\"" << config_.pluginName << "\" type=\"string\"/>\n";
    xml << "  <Attr id=\"PlugInCategory\" value=\"" << config_.pluginCategoryDesc << "\" type=\"string\"/>\n";
    xml << R"(  <Attr id="Name" value=")" << preset.name << "\" type=\"string\"/>\n";
    xml << R"(  <Attr id="MusicalCategory" value=")" << preset.category << "\" type=\"string\"/>\n";
    xml << R"(  <Attr id="MusicalInstrument" value=")" << preset.subcategory << "\" type=\"string\"/>\n";
    if (!preset.description.empty()) {
        xml << R"(  <Attr id="Comment" value=")" << preset.description << "\" type=\"string\"/>\n";
    }
    xml << "</MetaInfo>\n";
    std::string xmlStr = xml.str();

    // Open file stream for writing (overwrites existing)
    auto *stream = Steinberg::Vst::FileStream::open(preset.path.string().c_str(), "wb");
    if (!stream) {
        lastError_ = "Failed to open preset file for writing: " + preset.path.string();
        return false;
    }

    bool success = false;

    if (useStateProvider) {
        Steinberg::IBStream* componentStream = stateProvider_();
        if (!componentStream) {
            stream->release();
            lastError_ = "Failed to obtain component state";
            return false;
        }

        success = Steinberg::Vst::PresetFile::savePreset(
            stream,
            config_.processorUID,
            componentStream,
            nullptr,
            xmlStr.c_str(),
            static_cast<Steinberg::int32>(xmlStr.size())
        );

        componentStream->release();
    } else {
        success = Steinberg::Vst::PresetFile::savePreset(
            stream,
            config_.processorUID,
            processor_,
            controller_,
            xmlStr.c_str(),
            static_cast<Steinberg::int32>(xmlStr.size())
        );
    }

    stream->release();

    if (!success) {
        lastError_ = "Failed to overwrite preset data";
        return false;
    }

    lastError_.clear();
    return true;
}

bool PresetManager::deletePreset(const PresetInfo& preset) {
    if (preset.isFactory) {
        lastError_ = "Cannot delete factory presets";
        return false;
    }

    if (preset.path.empty() || !std::filesystem::exists(preset.path)) {
        lastError_ = "Preset file not found";
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::remove(preset.path, ec)) {
        lastError_ = "Failed to delete preset: " + ec.message();
        return false;
    }

    lastError_.clear();
    return true;
}

bool PresetManager::importPreset(const std::filesystem::path& sourcePath) {
    if (!std::filesystem::exists(sourcePath)) {
        lastError_ = "Source file not found";
        return false;
    }

    if (sourcePath.extension() != ".vstpreset") {
        lastError_ = "Invalid preset file type";
        return false;
    }

    // Create destination path
    auto userDir = getUserPresetDirectory();
    Platform::ensureDirectoryExists(userDir);

    auto destPath = userDir / sourcePath.filename();

    std::error_code ec;
    if (!std::filesystem::copy_file(sourcePath, destPath,
            std::filesystem::copy_options::skip_existing, ec)) {
        lastError_ = "Failed to import preset: " + ec.message();
        return false;
    }

    lastError_.clear();
    return true;
}

// =============================================================================
// Directory Access
// =============================================================================

std::filesystem::path PresetManager::getUserPresetDirectory() const {
    if (!userDirOverride_.empty()) {
        Platform::ensureDirectoryExists(userDirOverride_);
        return userDirOverride_;
    }
    auto path = Platform::getUserPresetDirectory(config_.pluginName);
    Platform::ensureDirectoryExists(path);
    return path;
}

std::filesystem::path PresetManager::getFactoryPresetDirectory() const {
    if (!factoryDirOverride_.empty()) {
        return factoryDirOverride_;
    }
    return Platform::getFactoryPresetDirectory(config_.pluginName);
}

// =============================================================================
// Validation
// =============================================================================

bool PresetManager::isValidPresetName(const std::string& name) {
    if (name.empty() || name.length() > 255) {
        return false;
    }

    // Check for invalid filesystem characters
    const std::string kInvalidChars = "/\\:*?\"<>|";
    return std::ranges::none_of(name, [&kInvalidChars](char c) {
        return kInvalidChars.find(c) != std::string::npos;
    });
}

// =============================================================================
// Metadata Helpers
// =============================================================================

bool PresetManager::writeMetadata(const std::filesystem::path& /*path*/, const PresetInfo& /*info*/) {
    // TODO: Implement XML metadata writing
    return true;
}

bool PresetManager::readMetadata(const std::filesystem::path& /*path*/, PresetInfo& /*info*/) {
    // TODO: Implement XML metadata reading
    return true;
}

} // namespace Krate::Plugins
