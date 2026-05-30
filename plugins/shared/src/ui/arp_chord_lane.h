#pragma once

// ==============================================================================
// ArpChordLane - Per-Step Chord Type Selection with Popup Menu
// ==============================================================================
// Thin concrete shell over EnumPopupArpLane<ChordLaneTraits>. Renders per-step
// chord type cells; left-click opens a COptionMenu popup with 5 chord types,
// right-click resets to None (index 0).
//
// Chord normalization: index / 4.0f (range 0-4, 5 values)
// Decode: clamp(round(normalized * 4.0f), 0, 4)
//
// Shared behavior lives in enum_popup_arp_lane.h; only the enum description
// (counts, labels, lane-type id, view names) differs from ArpInversionLane.
//
// Location: plugins/shared/src/ui/arp_chord_lane.h
// ==============================================================================

#include "enum_popup_arp_lane.h"

namespace Krate::Plugins {

// ==============================================================================
// ChordLaneTraits
// ==============================================================================

struct ChordLaneTraits {
    static constexpr int kValueCount = 5;
    static constexpr int kLaneTypeId = 6;  // ClipboardLaneType::kChord

    // Abbreviated labels for cell display
    static constexpr const char* kAbbrev[5] = {
        "--", "Dy", "Tri", "7th", "9th"
    };

    // Full names for COptionMenu popup entries
    static constexpr const char* kFullNames[5] = {
        "None", "Dyad", "Triad", "7th", "9th"
    };

    static constexpr const char* kViewName = "ArpChordLane";
    static constexpr const char* kDisplayName = "Arp Chord Lane";
};

// ==============================================================================
// ArpChordLane
// ==============================================================================

class ArpChordLane : public EnumPopupArpLane<ChordLaneTraits> {
public:
    // Backward-compatible named constants (chord-specific spelling).
    static constexpr int kChordTypeCount = ChordLaneTraits::kValueCount;
    static constexpr auto& kChordAbbrev = ChordLaneTraits::kAbbrev;
    static constexpr auto& kChordFullNames = ChordLaneTraits::kFullNames;

    ArpChordLane(const VSTGUI::CRect& size,
                 VSTGUI::IControlListener* listener,
                 int32_t tag)
        : EnumPopupArpLane<ChordLaneTraits>(size, listener, tag) {}

    CLASS_METHODS(ArpChordLane, CControl)
};

// =============================================================================
// ViewCreator Registration
// =============================================================================

using ArpChordLaneCreator = EnumPopupArpLaneCreator<ArpChordLane>;

inline ArpChordLaneCreator gArpChordLaneCreator;

} // namespace Krate::Plugins
