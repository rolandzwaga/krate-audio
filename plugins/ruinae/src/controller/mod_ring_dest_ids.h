#pragma once

// ==============================================================================
// Modulation Destination -> ParamID Mapping
// ==============================================================================
// Maps a voice modulation destination index to the VST parameter ID of the knob
// it drives. Two translation units need it -- controller_view_sync.cpp for the
// ModRingIndicator base-value sync, and controller_mod_matrix.cpp for the arc
// rebuild -- and they previously each carried their own static copy. A drift
// between the two desynced the ring indicators from the routes they display
// without any build error.
//
// This lives in Ruinae rather than in the shared ui/mod_matrix_types.h because
// it references Ruinae parameter IDs; the shared header is Krate::Plugins and
// cannot see them.

#include "plugin_ids.h"
#include "ui/mod_matrix_types.h"

#include "pluginterfaces/vst/vsttypes.h"

#include <array>

namespace Ruinae {

inline constexpr std::array<Steinberg::Vst::ParamID,
    Krate::Plugins::kNumVoiceDestinations> kVoiceDestParamIds = {{
    kFilterCutoffId,          // 0: Filter Cutoff
    kFilterResonanceId,       // 1: Filter Resonance
    kMixerPositionId,         // 2: Morph Position
    kDistortionDriveId,       // 3: Distortion Drive
    kTranceGateDepthId,       // 4: TranceGate Depth
    kOscATuneId,              // 5: OSC A Pitch
    kOscBTuneId,              // 6: OSC B Pitch
    kMixerTiltId,             // 7: Spectral Tilt
}};

static_assert(kVoiceDestParamIds.size() == Krate::Plugins::kVoiceDestNames.size(),
              "voice destination ParamID map must cover every destination name");

} // namespace Ruinae
