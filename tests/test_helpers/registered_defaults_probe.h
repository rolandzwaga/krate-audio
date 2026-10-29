#pragma once

// ==============================================================================
// Registered defaults probe
// ==============================================================================
// A parameter's registered default (ParameterInfo::defaultNormalizedValue) is
// where a host's "reset to default" puts it; its value right after
// Controller::initialize() is where a fresh instance starts. The two must agree,
// or a control starts on one value and resets to another. That happened to every
// dropdown built by createDropdownParameterWithDefault with a non-zero default:
// it set the starting value but left the registered default at the first option.
// Each plugin's controller lifecycle test lists the disagreements and requires none.

#include "public.sdk/source/vst/vsteditcontroller.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace Krate::TestSupport {

/// One line per parameter whose registered default differs from its value after
/// initialize(): "<id> <title>: registered default <d>, starts at <v>".
inline std::vector<std::string> parametersStartingOffTheirDefault(Steinberg::Vst::EditController& controller)
{
    std::vector<std::string> mismatches;
    const Steinberg::int32 count = controller.getParameterCount();
    for (Steinberg::int32 i = 0; i < count; ++i) {
        Steinberg::Vst::ParameterInfo info{};
        if (controller.getParameterInfo(i, info) != Steinberg::kResultOk)
            continue;
        const double start = controller.getParamNormalized(info.id);
        if (std::abs(start - info.defaultNormalizedValue) <= 1e-9)
            continue;

        std::string title;
        for (int c = 0; c < 128 && info.title[c] != 0; ++c)
            title += info.title[c] < 128 ? static_cast<char>(info.title[c]) : '?';
        char line[256];
        std::snprintf(line, sizeof(line), "%u %s: registered default %.4f, starts at %.4f",
                      static_cast<unsigned>(info.id), title.c_str(), info.defaultNormalizedValue, start);
        mismatches.emplace_back(line);
    }
    return mismatches;
}

} // namespace Krate::TestSupport
