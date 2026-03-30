#pragma once

// ==============================================================================
// Gradus Dropdown Mappings (arp-only subset)
// ==============================================================================

#include <array>

namespace Gradus {

// =============================================================================
// Arp Scale Type dropdown reorder
// =============================================================================
// The UI puts Chromatic first (index 0), but ScaleType enum has it at index 8.

inline constexpr int kArpScaleTypeCount = 16;
inline constexpr int kArpRootNoteCount = 12;

// Maps UI dropdown index to ScaleType enum value
inline constexpr std::array<int, 16> kArpScaleDisplayOrder = {
    8,   // 0: Chromatic
    0,   // 1: Major
    1,   // 2: Natural Minor
    2,   // 3: Harmonic Minor
    3,   // 4: Melodic Minor
    4,   // 5: Dorian
    6,   // 6: Phrygian
    7,   // 7: Lydian
    5,   // 8: Mixolydian
    9,   // 9: Locrian
    10,  // 10: Major Pentatonic
    11,  // 11: Minor Pentatonic
    12,  // 12: Blues
    13,  // 13: Whole Tone
    14,  // 14: Diminished (W-H)
    15,  // 15: Diminished (H-W)
};

// Maps ScaleType enum value to UI dropdown index (inverse of above)
inline constexpr std::array<int, 16> kArpScaleEnumToDisplay = {
    1,   // Major(0) -> UI 1
    2,   // NaturalMinor(1) -> UI 2
    3,   // HarmonicMinor(2) -> UI 3
    4,   // MelodicMinor(3) -> UI 4
    5,   // Dorian(4) -> UI 5
    8,   // Mixolydian(5) -> UI 8
    6,   // Phrygian(6) -> UI 6
    7,   // Lydian(7) -> UI 7
    0,   // Chromatic(8) -> UI 0
    9,   // Locrian(9) -> UI 9
    10,  // MajorPentatonic(10) -> UI 10
    11,  // MinorPentatonic(11) -> UI 11
    12,  // Blues(12) -> UI 12
    13,  // WholeTone(13) -> UI 13
    14,  // DiminishedWH(14) -> UI 14
    15,  // DiminishedHW(15) -> UI 15
};

} // namespace Gradus
