#pragma once

// ==============================================================================
// Note Value UI Constants - Centralized Dropdown Strings
// ==============================================================================
// Provides centralized UI strings for note value dropdowns.
// Copied from Iterum pattern, adapted for Ruinae namespace.
// ==============================================================================

#include <krate/dsp/core/note_value.h>
#include "pluginterfaces/base/ftypes.h"
#include "pluginterfaces/base/ustring.h"

namespace Ruinae::Parameters {

// Re-export DSP constants for convenience
using Krate::DSP::kNoteValueDropdownCount;
using Krate::DSP::kNoteValueDefaultIndex;

// =============================================================================
// Note Value Dropdown UI Strings
// =============================================================================
// Order MUST match kNoteValueDropdownMapping[] in dsp/core/note_value.h
// Total: 21 entries (indices 0-20), Default: index 10 (1/8 note)
// =============================================================================

inline const Steinberg::Vst::TChar* const kNoteValueDropdownStrings[] = {
    STR16("1/64T"),   // 0
    STR16("1/64"),    // 1
    STR16("1/64D"),   // 2
    STR16("1/32T"),   // 3
    STR16("1/32"),    // 4
    STR16("1/32D"),   // 5
    STR16("1/16T"),   // 6
    STR16("1/16"),    // 7
    STR16("1/16D"),   // 8
    STR16("1/8T"),    // 9
    STR16("1/8"),     // 10 (DEFAULT)
    STR16("1/8D"),    // 11
    STR16("1/4T"),    // 12
    STR16("1/4"),     // 13
    STR16("1/4D"),    // 14
    STR16("1/2T"),    // 15
    STR16("1/2"),     // 16
    STR16("1/2D"),    // 17
    STR16("1/1T"),    // 18
    STR16("1/1"),     // 19
    STR16("1/1D"),    // 20
};

} // namespace Ruinae::Parameters
