#pragma once

// ==============================================================================
// Note Value UI Constants (Innexus)
// ==============================================================================
// Thin re-export wrapper around the shared implementation in
// plugins/shared/src/ui/note_value_ui.h. Kept so existing Innexus code can
// continue to reference these constants unqualified inside Innexus::Parameters.
// ==============================================================================

#include "ui/note_value_ui.h"

namespace Innexus::Parameters {

using Krate::Plugins::Parameters::kNoteValueDropdownStrings;
using Krate::Plugins::Parameters::kNoteValueDropdownCount;
using Krate::Plugins::Parameters::kNoteValueDefaultIndex;

} // namespace Innexus::Parameters
