#pragma once

// ==============================================================================
// Parameter Helper Functions (Iterum)
// ==============================================================================
// Thin re-export wrapper around the shared implementation in
// plugins/shared/src/ui/parameter_helpers.h. Kept so existing Iterum code can
// continue to call these helpers unqualified inside the Iterum namespace.
// ==============================================================================

#include "ui/parameter_helpers.h"

namespace Iterum {

using Krate::Plugins::createDropdownParameter;
using Krate::Plugins::createDropdownParameterWithDefault;
using Krate::Plugins::createNoteValueDropdown;

} // namespace Iterum
