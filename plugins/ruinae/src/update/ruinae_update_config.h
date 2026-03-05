#pragma once

#include "update/update_checker_config.h"
#include "../version.h"

namespace Ruinae {

inline Krate::Plugins::UpdateCheckerConfig makeRuinaeUpdateConfig() {
    return Krate::Plugins::UpdateCheckerConfig{
        /*.pluginName =*/ stringPluginName,
        /*.currentVersion =*/ VERSION_STR,
        /*.endpointUrl =*/ "https://rolandzwaga.github.io/krate-audio/versions.json"
    };
}

} // namespace Ruinae
