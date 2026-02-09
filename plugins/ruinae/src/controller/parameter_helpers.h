#pragma once

// ==============================================================================
// Parameter Helper Functions
// ==============================================================================
// These helpers ensure correct parameter types are used for common patterns.
// Copied from Iterum pattern, adapted for Ruinae namespace.
//
// KEY INSIGHT: Basic Parameter::toPlain() returns normalized value unchanged!
// StringListParameter::toPlain() properly scales to integer indices.
// ==============================================================================

#include "public.sdk/source/vst/vstparameters.h"
#include "pluginterfaces/base/ustring.h"
#include <initializer_list>

namespace Ruinae {

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

} // namespace Ruinae
