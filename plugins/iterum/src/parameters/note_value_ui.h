#pragma once

// ==============================================================================
// Note Value UI Constants (Iterum)
// ==============================================================================
// Thin re-export wrapper around the shared implementation in
// plugins/shared/src/ui/note_value_ui.h. Kept so existing Iterum code can
// continue to reference these constants unqualified inside Iterum::Parameters.
// ==============================================================================

#include "ui/note_value_ui.h"

namespace Iterum::Parameters {

using Krate::Plugins::Parameters::kNoteValueDropdownStrings;
using Krate::Plugins::Parameters::kNoteValueDropdownCount;
using Krate::Plugins::Parameters::kNoteValueDefaultIndex;

} // namespace Iterum::Parameters
