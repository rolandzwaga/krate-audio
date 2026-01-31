// ==============================================================================
// MidiCCManager Contract
// ==============================================================================
// Manages MIDI CC-to-parameter mappings for the plugin.
// Placed in plugins/shared/ for reuse by Iterum and future plugins.
//
// FR-030: All parameters eligible for MIDI CC mapping
// FR-031: MIDI Learn via right-click context menu
// FR-032: Capture first CC and create global mapping
// FR-032a: "Save Mapping with Preset" checkbox
// FR-033: "Clear MIDI Learn" option
// FR-034: Hybrid persistence (global + per-preset)
// FR-035: Real-time parameter update from CC
// FR-036: Most recent mapping wins for CC conflicts
// FR-037: Cancel MIDI Learn with right-click or Escape
// FR-038-040: 14-bit MIDI CC support (CC pairs 0-31/32-63)
// ==============================================================================

#pragma once

#include "pluginterfaces/vst/vsttypes.h"

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace Krate::Plugins {

/// A single MIDI CC to parameter mapping
struct MidiCCMapping {
    uint8_t ccNumber = 0;                           // MIDI CC number (0-127, MSB for 14-bit)
    Steinberg::Vst::ParamID paramId = 0;            // Target parameter ID
    bool is14Bit = false;                           // true: ccNumber is MSB, ccNumber+32 is LSB
    bool isPerPreset = false;                       // true: stored with preset, false: global
};

/// Callback type for when a MIDI CC mapping changes a parameter
using MidiCCCallback = std::function<void(Steinberg::Vst::ParamID paramId, double normalizedValue)>;

/// Manages MIDI CC-to-parameter mappings with MIDI Learn support
class MidiCCManager {
public:
    MidiCCManager() = default;
    ~MidiCCManager() = default;

    // =========================================================================
    // Mapping Management
    // =========================================================================

    /// Add or update a global MIDI CC mapping
    /// @param ccNumber The MIDI CC number (0-127)
    /// @param paramId The target parameter ID
    /// @param is14Bit Whether to use 14-bit mode (CC 0-31 only)
    void addGlobalMapping(uint8_t ccNumber, Steinberg::Vst::ParamID paramId, bool is14Bit = false);

    /// Add or update a per-preset MIDI CC mapping
    void addPresetMapping(uint8_t ccNumber, Steinberg::Vst::ParamID paramId, bool is14Bit = false);

    /// Remove a global mapping by CC number
    void removeGlobalMapping(uint8_t ccNumber);

    /// Remove a per-preset mapping by CC number
    void removePresetMapping(uint8_t ccNumber);

    /// Remove all mappings (global and per-preset) for a specific parameter
    void removeMappingsForParam(Steinberg::Vst::ParamID paramId);

    /// Clear all per-preset mappings (called on preset change)
    void clearPresetMappings();

    /// Clear all mappings
    void clearAll();

    // =========================================================================
    // MIDI Learn
    // =========================================================================

    /// Start MIDI Learn mode for a specific parameter
    /// @param targetParamId The parameter to map the next received CC to
    void startLearn(Steinberg::Vst::ParamID targetParamId);

    /// Cancel an in-progress MIDI Learn session
    void cancelLearn();

    /// Check if MIDI Learn mode is currently active
    bool isLearning() const;

    /// Get the parameter ID currently being learned
    Steinberg::Vst::ParamID getLearnTargetParamId() const;

    // =========================================================================
    // MIDI CC Processing
    // =========================================================================

    /// Process an incoming MIDI CC message
    /// @param ccNumber The CC number (0-127)
    /// @param value The CC value (0-127)
    /// @param callback Called if the CC is mapped to a parameter
    /// @return true if the CC was handled (mapped or learned)
    bool processCCMessage(uint8_t ccNumber, uint8_t value, const MidiCCCallback& callback);

    // =========================================================================
    // Query
    // =========================================================================

    /// Get the active mapping for a CC number (per-preset overrides global)
    /// @param ccNumber The CC number to look up
    /// @param mapping Output: the active mapping (if found)
    /// @return true if a mapping exists for this CC number
    bool getMapping(uint8_t ccNumber, MidiCCMapping& mapping) const;

    /// Get the CC number mapped to a parameter (reverse lookup)
    /// @param paramId The parameter to look up
    /// @param ccNumber Output: the CC number (if found)
    /// @return true if the parameter has a CC mapping
    bool getCCForParam(Steinberg::Vst::ParamID paramId, uint8_t& ccNumber) const;

    /// Get all active mappings (per-preset overrides global for same param)
    std::vector<MidiCCMapping> getActiveMappings() const;

    // =========================================================================
    // IMidiMapping Support
    // =========================================================================

    /// Called by getMidiControllerAssignment() to check if a CC is mapped
    /// @param ccNumber The MIDI CC number to check
    /// @param paramId Output: the mapped parameter ID
    /// @return true if the CC is mapped to a parameter
    bool getMidiControllerAssignment(uint8_t ccNumber, Steinberg::Vst::ParamID& paramId) const;

    // =========================================================================
    // Serialization
    // =========================================================================

    /// Serialize global mappings to a byte buffer
    /// @return Serialized bytes (can be written to IBStream)
    std::vector<uint8_t> serializeGlobalMappings() const;

    /// Deserialize global mappings from a byte buffer
    /// @return true on success
    bool deserializeGlobalMappings(const uint8_t* data, size_t size);

    /// Serialize per-preset mappings to a byte buffer
    std::vector<uint8_t> serializePresetMappings() const;

    /// Deserialize per-preset mappings from a byte buffer
    bool deserializePresetMappings(const uint8_t* data, size_t size);

private:
    std::unordered_map<uint8_t, MidiCCMapping> globalMappings_;
    std::unordered_map<uint8_t, MidiCCMapping> presetMappings_;
    std::unordered_map<Steinberg::Vst::ParamID, uint8_t> paramToCC_;

    // MIDI Learn state
    bool learnModeActive_ = false;
    Steinberg::Vst::ParamID learnTargetParamId_ = 0;

    // 14-bit MSB tracking
    uint8_t lastMSB_[32] = {};  // Only CC 0-31 have LSB pairs
};

} // namespace Krate::Plugins
