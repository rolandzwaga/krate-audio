#pragma once

// ==============================================================================
// PresetManager - Preset File Operations (Shared)
// ==============================================================================
// Handles all preset file operations including scanning, loading, saving,
// importing, and deleting presets. Generalized via PresetManagerConfig.
//
// Thread Safety: All methods must be called from UI thread only.
//
// Constitution Compliance:
// - Principle II: No audio thread involvement
// - Principle VI: Cross-platform via std::filesystem
// ==============================================================================

#include "preset_info.h"
#include "preset_manager_config.h"
#include <string>
#include <string_view>
#include <vector>
#include <filesystem>
#include <functional>

namespace Steinberg {
    class IBStream;
}

namespace Steinberg::Vst {
    class IComponent;
    class IEditController;
}

namespace Krate::Plugins {

class PresetManager {
public:
    using PresetList = std::vector<PresetInfo>;

    /// Callback type for providing component state stream
    /// Returns an IBStream* (caller takes ownership), or nullptr on failure
    using StateProvider = std::function<Steinberg::IBStream*()>;

    /// Callback type for loading component state with host notification
    /// Takes an IBStream containing component state, applies it via controller with performEdit
    /// @return true on success
    using LoadProvider = std::function<bool(Steinberg::IBStream*)>;

    /// Constructor
    /// @param config Plugin-specific configuration (processorUID, pluginName, etc.)
    /// @param processor VST3 processor component for state access
    /// @param controller VST3 edit controller for state sync
    /// @param userDirOverride Optional override for user preset directory (for testing)
    /// @param factoryDirOverride Optional override for factory preset directory (for testing)
    explicit PresetManager(
        PresetManagerConfig config,
        Steinberg::Vst::IComponent* processor,
        Steinberg::Vst::IEditController* controller,
        std::filesystem::path userDirOverride = {},
        std::filesystem::path factoryDirOverride = {}
    );

    ~PresetManager();

    // ==========================================================================
    // Scanning
    // ==========================================================================

    /// Scan all preset directories and return combined list
    PresetList scanPresets();

    /// Get presets filtered by subcategory.
    /// Empty string returns ALL presets (equivalent to "All" UI filter).
    /// Non-empty string returns only presets matching that subcategory.
    PresetList getPresetsForSubcategory(const std::string& subcategory) const;

    /// Search presets by name (case-insensitive)
    PresetList searchPresets(std::string_view query) const;

    // ==========================================================================
    // Load/Save
    // ==========================================================================

    /// Load a preset, restoring all parameters
    bool loadPreset(const PresetInfo& preset);

    /// Save current state as new preset
    bool savePreset(
        const std::string& name,
        const std::string& subcategory,
        const std::string& description = ""
    );

    /// Overwrite an existing user preset with current state
    /// Factory presets cannot be overwritten
    bool overwritePreset(const PresetInfo& preset);

    /// Delete a user preset
    /// Factory presets cannot be deleted
    bool deletePreset(const PresetInfo& preset);

    /// Import a preset from external location
    bool importPreset(const std::filesystem::path& sourcePath);

    // ==========================================================================
    // Directory Access
    // ==========================================================================

    /// Get user preset directory path (creates if needed)
    std::filesystem::path getUserPresetDirectory() const;

    /// Get factory preset directory path
    std::filesystem::path getFactoryPresetDirectory() const;

    // ==========================================================================
    // Validation
    // ==========================================================================

    /// Validate preset name for filesystem compatibility
    static bool isValidPresetName(const std::string& name);

    /// Get last error message
    std::string getLastError() const { return lastError_; }

    /// Set callback for obtaining component state stream
    void setStateProvider(StateProvider provider) { stateProvider_ = std::move(provider); }

    /// Set callback for loading component state with host notification
    void setLoadProvider(LoadProvider provider) { loadProvider_ = std::move(provider); }

    /// Get the configuration
    const PresetManagerConfig& getConfig() const { return config_; }

private:
    PresetManagerConfig config_;
    Steinberg::Vst::IComponent* processor_ = nullptr;
    Steinberg::Vst::IEditController* controller_ = nullptr;
    StateProvider stateProvider_;
    LoadProvider loadProvider_;
    PresetList cachedPresets_;
    std::string lastError_;
    std::filesystem::path userDirOverride_;
    std::filesystem::path factoryDirOverride_;

    // Scanning helpers
    void scanDirectory(const std::filesystem::path& dir, bool isFactory);
    PresetInfo parsePresetFile(const std::filesystem::path& path, bool isFactory) const;

    // Metadata helpers
    static bool writeMetadata(const std::filesystem::path& path, const PresetInfo& info);
    static bool readMetadata(const std::filesystem::path& path, PresetInfo& info);
};

} // namespace Krate::Plugins
