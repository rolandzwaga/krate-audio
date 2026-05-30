#pragma once

// ==============================================================================
// Parameter Display Formatting Helpers (Iterum)
// ==============================================================================
// Every per-mode formatXxxParam() switch case repeats the same leaf idiom:
//   char8 text[32];
//   snprintf(text, sizeof(text), "<fmt>", value);
//   Steinberg::UString(string, 128).fromAscii(text);
//   return kResultOk;
// formatParamText() collapses that to a single call while reproducing the exact
// snprintf output. Denorm math stays in each pack; only the leaf format unifies.
// ==============================================================================

#include "pluginterfaces/base/ftypes.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/vsttypes.h"

#include <cstdarg>
#include <cstdio>

namespace Iterum {

// printf-style render directly into a VST String128. Always returns kResultOk so
// a case body can be written as: `return formatParamText(string, "%.1f ms", ms);`
inline Steinberg::tresult formatParamText(Steinberg::Vst::String128 string,
                                          const char* fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

inline Steinberg::tresult formatParamText(Steinberg::Vst::String128 string,
                                          const char* fmt, ...) {
    Steinberg::char8 text[32];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    Steinberg::UString(string, 128).fromAscii(text);
    return Steinberg::kResultOk;
}

} // namespace Iterum
