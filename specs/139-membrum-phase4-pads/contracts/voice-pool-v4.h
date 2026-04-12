// Contract: VoicePool v4 API changes for Membrum Phase 4
// This file documents the API changes to VoicePool for per-pad dispatch
// and multi-bus output.

#pragma once

// The following methods are ADDED or MODIFIED in VoicePool for Phase 4:

namespace Membrum {

// ============================================================
// REMOVED API (replaced by per-pad dispatch):
// ============================================================
//
// void setSharedVoiceParams(float mat, float sz, float dec, float sp, float lv);
// void setSharedExciterType(ExciterType type);
// void setSharedBodyModel(BodyModelType model);
// struct SharedParams { ... };   // entire struct removed
// SharedParams sharedParams_;    // field removed
//
// void applySharedParamsToSlot(int slot);  // replaced by applyPadConfigToSlot

// ============================================================
// NEW API:
// ============================================================

// -- Per-pad config storage (replaces SharedParams) --
//
// PadConfig padConfigs_[kNumPads];  // 32 pre-allocated pad configs
//
// /// Update a field in a specific pad's config. Called from processParameterChanges.
// void setPadConfigField(int padIndex, int offset, float normalizedValue) noexcept;
//
// /// Set a discrete selector for a pad. Handles ExciterType/BodyModel.
// void setPadConfigSelector(int padIndex, int offset, int discreteValue) noexcept;
//
// /// Read-only access to pad configs (for state serialization).
// const PadConfig& padConfig(int padIndex) const noexcept;
//
// /// Mutable access (for state deserialization / preset load).
// PadConfig& padConfigMut(int padIndex) noexcept;

// -- Multi-bus output --
//
// /// Extended processBlock with multi-bus output support.
// /// auxBuffers: array of [kMaxOutputBuses] stereo buffer pairs.
// ///   auxBuffers[0] is the main output (same as outL/outR).
// ///   auxBuffers[1..15] are auxiliary outputs (may be nullptr if inactive).
// /// busActive: boolean array indicating which buses are active.
// void processBlock(
//     float* outL, float* outR,
//     float** auxL, float** auxR,
//     const bool* busActive,
//     int numOutputBuses,
//     int numSamples) noexcept;

// -- Per-pad config application (replaces applySharedParamsToSlot) --
//
// /// Apply pad N's configuration to a voice slot at noteOn time.
// /// Called internally by noteOn() using the midiNote-to-pad mapping.
// void applyPadConfigToSlot(int slot, int padIndex) noexcept;

// -- Choke group table update --
//
// /// Set the choke group for a specific pad (replaces setChokeGroup global).
// void setPadChokeGroup(int padIndex, std::uint8_t group) noexcept;

} // namespace Membrum
