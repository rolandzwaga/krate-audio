#pragma once

// ==============================================================================
// Vorago Update-Check Configuration (FR-052)   Plan section 2.8
// ==============================================================================
// SHIPPED COMPILED BUT UNUSED: no UpdateChecker instance exists in this plugin.
// Included by nothing else; the ONLY things that compile it are the static_assert
// touch point in controller.cpp (a .h in a CMake source list is HEADER_FILE_ONLY)
// and the PresetConfigIsLive section of editor_lifecycle_test.cpp.
// Model: plugins/seraphis/src/update/seraphis_update_config.h:21-27.
// ==============================================================================

#include "update/update_checker_config.h"
#include "../version.h"

namespace Vorago {

inline Krate::Plugins::UpdateCheckerConfig makeVoragoUpdateConfig() {
    return Krate::Plugins::UpdateCheckerConfig{
        /*.pluginName     =*/stringPluginName,
        /*.currentVersion =*/VERSION_STR,
        /*.endpointUrl    =*/"https://rolandzwaga.github.io/krate-audio/versions.json"};
}

}  // namespace Vorago
