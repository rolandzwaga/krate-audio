#pragma once

// ==============================================================================
// ArpInversionLane - Per-Step Inversion Selection with Popup Menu
// ==============================================================================
// Thin concrete shell over EnumPopupArpLane<InversionLaneTraits>. Renders
// per-step inversion cells; left-click opens a COptionMenu popup with 4
// inversion types, right-click resets to Root (index 0).
//
// Inversion normalization: index / 3.0f (range 0-3, 4 values)
// Decode: clamp(round(normalized * 3.0f), 0, 3)
//
// Shared behavior lives in enum_popup_arp_lane.h; only the enum description
// (counts, labels, lane-type id, view names) differs from ArpChordLane.
//
// Location: plugins/shared/src/ui/arp_inversion_lane.h
// ==============================================================================

#include "enum_popup_arp_lane.h"

namespace Krate::Plugins {

// ==============================================================================
// InversionLaneTraits
// ==============================================================================

struct InversionLaneTraits {
    static constexpr int kValueCount = 4;
    static constexpr int kLaneTypeId = 7;  // ClipboardLaneType::kInversion

    // Abbreviated labels for cell display
    static constexpr const char* kAbbrev[4] = {
        "Rt", "1st", "2nd", "3rd"
    };

    // Full names for COptionMenu popup entries
    static constexpr const char* kFullNames[4] = {
        "Root", "1st Inv", "2nd Inv", "3rd Inv"
    };

    static constexpr const char* kViewName = "ArpInversionLane";
    static constexpr const char* kDisplayName = "Arp Inversion Lane";
};

// ==============================================================================
// ArpInversionLane
// ==============================================================================

class ArpInversionLane : public EnumPopupArpLane<InversionLaneTraits> {
public:
    // Backward-compatible named constants (inversion-specific spelling).
    static constexpr int kInversionCount = InversionLaneTraits::kValueCount;
    static constexpr auto& kInversionAbbrev = InversionLaneTraits::kAbbrev;
    static constexpr auto& kInversionFullNames = InversionLaneTraits::kFullNames;

    ArpInversionLane(const VSTGUI::CRect& size,
                     VSTGUI::IControlListener* listener,
                     int32_t tag)
        : EnumPopupArpLane<InversionLaneTraits>(size, listener, tag) {}

    CLASS_METHODS(ArpInversionLane, CControl)
};

// =============================================================================
// ViewCreator Registration
// =============================================================================

using ArpInversionLaneCreator = EnumPopupArpLaneCreator<ArpInversionLane>;

inline ArpInversionLaneCreator gArpInversionLaneCreator;

} // namespace Krate::Plugins
