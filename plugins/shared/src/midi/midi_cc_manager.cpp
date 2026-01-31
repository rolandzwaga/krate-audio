// ==============================================================================
// MidiCCManager Implementation
// ==============================================================================
// Manages MIDI CC-to-parameter mappings for the plugin.
//
// FR-030 to FR-040: MIDI CC mapping, MIDI Learn, 14-bit CC support
// ==============================================================================

#include "midi/midi_cc_manager.h"

#include <algorithm>
#include <cstring>

namespace Krate::Plugins {

// =============================================================================
// Mapping Management
// =============================================================================

void MidiCCManager::addGlobalMapping(uint8_t ccNumber, Steinberg::Vst::ParamID paramId, bool is14Bit) {
    // FR-036: Most recent mapping wins - remove any existing mapping for this CC
    auto existingIt = globalMappings_.find(ccNumber);
    if (existingIt != globalMappings_.end()) {
        paramToCC_.erase(existingIt->second.paramId);
    }

    // Remove any existing CC assignment for this parameter
    auto paramIt = paramToCC_.find(paramId);
    if (paramIt != paramToCC_.end()) {
        globalMappings_.erase(paramIt->second);
    }

    MidiCCMapping mapping;
    mapping.ccNumber = ccNumber;
    mapping.paramId = paramId;
    mapping.is14Bit = is14Bit && (ccNumber < 32);  // 14-bit only valid for CC 0-31
    mapping.isPerPreset = false;

    globalMappings_[ccNumber] = mapping;
    paramToCC_[paramId] = ccNumber;
}

void MidiCCManager::addPresetMapping(uint8_t ccNumber, Steinberg::Vst::ParamID paramId, bool is14Bit) {
    MidiCCMapping mapping;
    mapping.ccNumber = ccNumber;
    mapping.paramId = paramId;
    mapping.is14Bit = is14Bit && (ccNumber < 32);
    mapping.isPerPreset = true;

    presetMappings_[ccNumber] = mapping;
}

void MidiCCManager::removeGlobalMapping(uint8_t ccNumber) {
    auto it = globalMappings_.find(ccNumber);
    if (it != globalMappings_.end()) {
        paramToCC_.erase(it->second.paramId);
        globalMappings_.erase(it);
    }
}

void MidiCCManager::removePresetMapping(uint8_t ccNumber) {
    presetMappings_.erase(ccNumber);
}

void MidiCCManager::removeMappingsForParam(Steinberg::Vst::ParamID paramId) {
    // Remove from global mappings
    for (auto it = globalMappings_.begin(); it != globalMappings_.end(); ) {
        if (it->second.paramId == paramId) {
            it = globalMappings_.erase(it);
        } else {
            ++it;
        }
    }

    // Remove from preset mappings
    for (auto it = presetMappings_.begin(); it != presetMappings_.end(); ) {
        if (it->second.paramId == paramId) {
            it = presetMappings_.erase(it);
        } else {
            ++it;
        }
    }

    // Remove from reverse lookup
    paramToCC_.erase(paramId);
}

void MidiCCManager::clearPresetMappings() {
    presetMappings_.clear();
}

void MidiCCManager::clearAll() {
    globalMappings_.clear();
    presetMappings_.clear();
    paramToCC_.clear();
    learnModeActive_ = false;
    learnTargetParamId_ = 0;
    std::memset(lastMSB_, 0, sizeof(lastMSB_));
}

// =============================================================================
// MIDI Learn
// =============================================================================

void MidiCCManager::startLearn(Steinberg::Vst::ParamID targetParamId) {
    learnModeActive_ = true;
    learnTargetParamId_ = targetParamId;
}

void MidiCCManager::cancelLearn() {
    learnModeActive_ = false;
    learnTargetParamId_ = 0;
}

bool MidiCCManager::isLearning() const {
    return learnModeActive_;
}

Steinberg::Vst::ParamID MidiCCManager::getLearnTargetParamId() const {
    return learnTargetParamId_;
}

// =============================================================================
// MIDI CC Processing
// =============================================================================

bool MidiCCManager::processCCMessage(uint8_t ccNumber, uint8_t value, const MidiCCCallback& callback) {
    // Handle MIDI Learn mode first
    if (learnModeActive_) {
        // Don't learn from LSB CCs (32-63) directly
        if (ccNumber >= 32 && ccNumber <= 63) {
            return false;
        }

        addGlobalMapping(ccNumber, learnTargetParamId_, ccNumber < 32);
        learnModeActive_ = false;

        // Also send the initial value
        if (callback) {
            double normalized = static_cast<double>(value) / 127.0;
            callback(learnTargetParamId_, normalized);
        }

        learnTargetParamId_ = 0;
        return true;
    }

    // Check if this is an LSB message for a 14-bit pair (CC 32-63)
    if (ccNumber >= 32 && ccNumber <= 63) {
        uint8_t msbCC = ccNumber - 32;
        MidiCCMapping mapping;
        if (getMapping(msbCC, mapping) && mapping.is14Bit) {
            // Combine MSB and LSB for 14-bit value
            uint16_t combined = (static_cast<uint16_t>(lastMSB_[msbCC]) << 7) | value;
            double normalized = static_cast<double>(combined) / 16383.0;

            if (callback) {
                callback(mapping.paramId, normalized);
            }
            return true;
        }
        return false;
    }

    // Track MSB for 14-bit pairs
    if (ccNumber < 32) {
        lastMSB_[ccNumber] = value;
    }

    // Look up active mapping (per-preset overrides global)
    MidiCCMapping mapping;
    if (!getMapping(ccNumber, mapping)) {
        return false;
    }

    // Calculate normalized value
    double normalized;
    if (mapping.is14Bit) {
        // For 14-bit, use only MSB until LSB arrives (7-bit fallback, FR-040)
        normalized = static_cast<double>(value) / 127.0;
    } else {
        normalized = static_cast<double>(value) / 127.0;
    }

    if (callback) {
        callback(mapping.paramId, normalized);
    }
    return true;
}

// =============================================================================
// Query
// =============================================================================

bool MidiCCManager::getMapping(uint8_t ccNumber, MidiCCMapping& mapping) const {
    // Per-preset overrides global (FR-034)
    auto presetIt = presetMappings_.find(ccNumber);
    if (presetIt != presetMappings_.end()) {
        mapping = presetIt->second;
        return true;
    }

    auto globalIt = globalMappings_.find(ccNumber);
    if (globalIt != globalMappings_.end()) {
        mapping = globalIt->second;
        return true;
    }

    return false;
}

bool MidiCCManager::getCCForParam(Steinberg::Vst::ParamID paramId, uint8_t& ccNumber) const {
    // Check preset mappings first
    for (const auto& [cc, mapping] : presetMappings_) {
        if (mapping.paramId == paramId) {
            ccNumber = cc;
            return true;
        }
    }

    auto it = paramToCC_.find(paramId);
    if (it != paramToCC_.end()) {
        ccNumber = it->second;
        return true;
    }

    return false;
}

std::vector<MidiCCMapping> MidiCCManager::getActiveMappings() const {
    std::vector<MidiCCMapping> result;

    // Start with all global mappings
    for (const auto& [cc, mapping] : globalMappings_) {
        result.push_back(mapping);
    }

    // Add per-preset mappings, overriding global for same CC
    for (const auto& [cc, mapping] : presetMappings_) {
        // Remove any global mapping for same CC
        result.erase(
            std::remove_if(result.begin(), result.end(),
                [cc](const MidiCCMapping& m) { return m.ccNumber == cc; }),
            result.end());
        result.push_back(mapping);
    }

    return result;
}

// =============================================================================
// IMidiMapping Support
// =============================================================================

bool MidiCCManager::getMidiControllerAssignment(uint8_t ccNumber, Steinberg::Vst::ParamID& paramId) const {
    MidiCCMapping mapping;
    if (getMapping(ccNumber, mapping)) {
        paramId = mapping.paramId;
        return true;
    }
    return false;
}

// =============================================================================
// Serialization
// =============================================================================

// Format: uint32_t count, then for each: uint8_t cc, uint32_t paramId, uint8_t flags
// flags: bit 0 = is14Bit, bit 1 = isPerPreset

std::vector<uint8_t> MidiCCManager::serializeGlobalMappings() const {
    std::vector<uint8_t> data;

    uint32_t count = static_cast<uint32_t>(globalMappings_.size());
    data.resize(4);
    std::memcpy(data.data(), &count, 4);

    for (const auto& [cc, mapping] : globalMappings_) {
        data.push_back(mapping.ccNumber);

        uint32_t paramId = mapping.paramId;
        size_t offset = data.size();
        data.resize(data.size() + 4);
        std::memcpy(data.data() + offset, &paramId, 4);

        uint8_t flags = 0;
        if (mapping.is14Bit) flags |= 0x01;
        if (mapping.isPerPreset) flags |= 0x02;
        data.push_back(flags);
    }

    return data;
}

bool MidiCCManager::deserializeGlobalMappings(const uint8_t* data, size_t size) {
    if (!data || size < 4) return false;

    uint32_t count = 0;
    std::memcpy(&count, data, 4);
    size_t offset = 4;

    // Sanity check: each mapping is 6 bytes (1 + 4 + 1)
    if (size < 4 + count * 6) return false;

    globalMappings_.clear();
    paramToCC_.clear();

    for (uint32_t i = 0; i < count; ++i) {
        MidiCCMapping mapping;
        mapping.ccNumber = data[offset++];

        std::memcpy(&mapping.paramId, data + offset, 4);
        offset += 4;

        uint8_t flags = data[offset++];
        mapping.is14Bit = (flags & 0x01) != 0;
        mapping.isPerPreset = (flags & 0x02) != 0;

        globalMappings_[mapping.ccNumber] = mapping;
        paramToCC_[mapping.paramId] = mapping.ccNumber;
    }

    return true;
}

std::vector<uint8_t> MidiCCManager::serializePresetMappings() const {
    std::vector<uint8_t> data;

    uint32_t count = static_cast<uint32_t>(presetMappings_.size());
    data.resize(4);
    std::memcpy(data.data(), &count, 4);

    for (const auto& [cc, mapping] : presetMappings_) {
        data.push_back(mapping.ccNumber);

        uint32_t paramId = mapping.paramId;
        size_t offset = data.size();
        data.resize(data.size() + 4);
        std::memcpy(data.data() + offset, &paramId, 4);

        uint8_t flags = 0;
        if (mapping.is14Bit) flags |= 0x01;
        if (mapping.isPerPreset) flags |= 0x02;
        data.push_back(flags);
    }

    return data;
}

bool MidiCCManager::deserializePresetMappings(const uint8_t* data, size_t size) {
    if (!data || size < 4) return false;

    uint32_t count = 0;
    std::memcpy(&count, data, 4);
    size_t offset = 4;

    if (size < 4 + count * 6) return false;

    presetMappings_.clear();

    for (uint32_t i = 0; i < count; ++i) {
        MidiCCMapping mapping;
        mapping.ccNumber = data[offset++];

        std::memcpy(&mapping.paramId, data + offset, 4);
        offset += 4;

        uint8_t flags = data[offset++];
        mapping.is14Bit = (flags & 0x01) != 0;
        mapping.isPerPreset = (flags & 0x02) != 0;

        presetMappings_[mapping.ccNumber] = mapping;
    }

    return true;
}

} // namespace Krate::Plugins
