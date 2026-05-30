#pragma once

// ==============================================================================
// Shared VST3 Plugin Factory Macro
// ==============================================================================
// Every Krate plugin's entry.cpp declares a byte-identical two-class factory
// (processor + controller) differing only by namespace, display-name token, and
// subcategory string. KRATE_DEFINE_PLUGIN_FACTORY collapses that boilerplate.
//
// Usage (in <plugin>/src/entry.cpp, AFTER including pluginfactory.h and any
// local `#define stringPluginName "..."`):
//
//   KRATE_DEFINE_PLUGIN_FACTORY(Iterum, stringPluginName, Iterum::kSubCategories)
//
// Parameters:
//   Namespace      - plugin namespace providing kProcessorUID / kControllerUID
//                    and Processor::createInstance / Controller::createInstance.
//   NameToken      - the plugin display-name string literal (or a macro/define
//                    that expands to one, e.g. stringPluginName). The controller
//                    name is formed by literal concatenation: NameToken "Controller".
//   SubCategories  - the processor subcategory string (e.g. Namespace::kSubCategories).
//
// The macro is expanded inside entry.cpp's translation unit, so the unqualified
// SDK identifiers (PClassInfo, kVstAudioEffectClass, ...) resolve exactly as they
// did in the hand-written factories.
// ==============================================================================

#include "public.sdk/source/main/pluginfactory.h"

#define KRATE_DEFINE_PLUGIN_FACTORY(Namespace, NameToken, SubCategories)            \
    BEGIN_FACTORY_DEF(stringCompanyName, stringVendorURL, stringVendorEmail)        \
                                                                                   \
        DEF_CLASS2(                                                                 \
            INLINE_UID_FROM_FUID(Namespace::kProcessorUID),                         \
            PClassInfo::kManyInstances,                                             \
            kVstAudioEffectClass,                                                   \
            NameToken,                                                              \
            Steinberg::Vst::kDistributable,                                         \
            SubCategories,                                                          \
            FULL_VERSION_STR,                                                       \
            kVstVersionString,                                                      \
            Namespace::Processor::createInstance)                                   \
                                                                                   \
        DEF_CLASS2(                                                                 \
            INLINE_UID_FROM_FUID(Namespace::kControllerUID),                        \
            PClassInfo::kManyInstances,                                             \
            kVstComponentControllerClass,                                           \
            NameToken "Controller",                                                 \
            0,                                                                      \
            "",                                                                     \
            FULL_VERSION_STR,                                                       \
            kVstVersionString,                                                      \
            Namespace::Controller::createInstance)                                  \
                                                                                   \
    END_FACTORY
