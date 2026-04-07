#pragma once

// Thin wrapper — actual implementation lives in plugins/shared/src/ui/parameter_helpers.h
#include "ui/parameter_helpers.h"

namespace Gradus {
    using Krate::Plugins::createDropdownParameter;
    using Krate::Plugins::createDropdownParameterWithDefault;
    using Krate::Plugins::createNoteValueDropdown;
} // namespace Gradus
