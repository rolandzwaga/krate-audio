#pragma once

// ==============================================================================
// Parameter Helper Functions (Shared)
// ==============================================================================
// These helpers ensure correct parameter types are used for common patterns.
// KEY INSIGHT: Basic Parameter::toPlain() returns normalized value unchanged!
// StringListParameter::toPlain() properly scales to integer indices.
// ==============================================================================

#include "public.sdk/source/vst/vstparameters.h"
#include "pluginterfaces/base/ustring.h"
#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace Krate::Plugins {

// ==============================================================================
// createDropdownParameter - For COptionMenu / discrete list parameters
// ==============================================================================

inline Steinberg::Vst::StringListParameter* createDropdownParameter(
    const Steinberg::Vst::TChar* title,
    Steinberg::Vst::ParamID id,
    std::initializer_list<const Steinberg::Vst::TChar*> options) {

    auto* param = new Steinberg::Vst::StringListParameter(
        title,
        id,
        nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate |
        Steinberg::Vst::ParameterInfo::kIsList
    );

    for (const auto* option : options) {
        param->appendString(option);
    }

    return param;
}

// ==============================================================================
// createDropdownParameterWithDefault - Same as above but with custom default
// ==============================================================================

inline Steinberg::Vst::StringListParameter* createDropdownParameterWithDefault(
    const Steinberg::Vst::TChar* title,
    Steinberg::Vst::ParamID id,
    int32_t defaultIndex,
    std::initializer_list<const Steinberg::Vst::TChar*> options) {

    auto* param = new Steinberg::Vst::StringListParameter(
        title,
        id,
        nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate |
        Steinberg::Vst::ParameterInfo::kIsList
    );

    for (const auto* option : options) {
        param->appendString(option);
    }

    if (defaultIndex >= 0 && defaultIndex <= param->getInfo().stepCount) {
        param->setNormalized(param->toNormalized(static_cast<Steinberg::Vst::ParamValue>(defaultIndex)));
    }

    return param;
}

// ==============================================================================
// Logarithmic normalized <-> units mappings
// ==============================================================================
// The pair `units = mn * pow(mx/mn, x)` and its inverse was hand-reimplemented
// in every parameter module that exposes a log-scaled control, at inconsistent
// precision. Both directions clamp on the way in and on the way out, so a
// round-trip through them is stable at the endpoints.

inline double logMapFromNormalized(double normalized, double mn, double mx) {
    const double clamped = std::clamp(normalized, 0.0, 1.0);
    return std::clamp(mn * std::pow(mx / mn, clamped), mn, mx);
}

inline double logMapToNormalized(double units, double mn, double mx) {
    const double clamped = std::clamp(units, mn, mx);
    return std::clamp(std::log(clamped / mn) / std::log(mx / mn), 0.0, 1.0);
}

// ==============================================================================
// Pointer + count overloads
// ==============================================================================
// For callers that already hold the labels in a canonical table rather than
// spelling them out at the call site. Keeping one table and passing it here is
// what stops the same list existing in two places and drifting.

inline Steinberg::Vst::StringListParameter* createDropdownParameter(
    const Steinberg::Vst::TChar* title,
    Steinberg::Vst::ParamID id,
    const Steinberg::Vst::TChar* const* strings,
    int count) {

    auto* param = new Steinberg::Vst::StringListParameter(
        title,
        id,
        nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate |
        Steinberg::Vst::ParameterInfo::kIsList
    );

    for (int i = 0; i < count; ++i) {
        param->appendString(strings[i]);
    }

    return param;
}

inline Steinberg::Vst::StringListParameter* createDropdownParameterWithDefault(
    const Steinberg::Vst::TChar* title,
    Steinberg::Vst::ParamID id,
    int32_t defaultIndex,
    const Steinberg::Vst::TChar* const* strings,
    int count) {

    auto* param = createDropdownParameter(title, id, strings, count);

    if (defaultIndex >= 0 && defaultIndex <= param->getInfo().stepCount) {
        param->setNormalized(param->toNormalized(static_cast<Steinberg::Vst::ParamValue>(defaultIndex)));
    }

    return param;
}

// ==============================================================================
// createNoteValueDropdown - For tempo-synced note value dropdowns
// ==============================================================================

inline Steinberg::Vst::StringListParameter* createNoteValueDropdown(
    const Steinberg::Vst::TChar* title,
    Steinberg::Vst::ParamID id,
    const Steinberg::Vst::TChar* const* strings,
    int count,
    int defaultIndex) {

    auto* param = new Steinberg::Vst::StringListParameter(
        title,
        id,
        nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate |
        Steinberg::Vst::ParameterInfo::kIsList
    );

    for (int i = 0; i < count; ++i) {
        param->appendString(strings[i]);
    }

    if (defaultIndex >= 0 && defaultIndex <= param->getInfo().stepCount) {
        param->setNormalized(param->toNormalized(static_cast<Steinberg::Vst::ParamValue>(defaultIndex)));
    }

    return param;
}

} // namespace Krate::Plugins
