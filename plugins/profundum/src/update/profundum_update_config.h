#pragma once

// ==============================================================================
// Profundum Update-Check Configuration (FR-052)   Plan section 2.8
// ==============================================================================
// The controller's live UpdateChecker is built from this config (2026-10-26; the
// FR-052 "compiled but unused" ruling is superseded, as Vorago's was on 2026-10-11).
// The PresetConfigIsLive section of editor_lifecycle_test.cpp checks its fields.
// Model: plugins/vorago/src/update/vorago_update_config.h:16-25.
// ==============================================================================

#include "update/update_checker_config.h"
#include "../version.h"

namespace Profundum {

inline Krate::Plugins::UpdateCheckerConfig makeProfundumUpdateConfig() {
    return Krate::Plugins::UpdateCheckerConfig{
        /*.pluginName     =*/stringPluginName,
        /*.currentVersion =*/VERSION_STR,
        /*.endpointUrl    =*/"https://rolandzwaga.github.io/krate-audio/versions.json"};
}

}  // namespace Profundum
