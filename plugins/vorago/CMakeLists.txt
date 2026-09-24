# ==============================================================================
# Vorago VST3 Plugin
# ==============================================================================
# Constitution Principle VII: Modern CMake with target-based configuration
# Links against shared KrateDSP library (Vorago DSP = Phases 1-10a of dsp/).
# Phase 11 (specs/vorago-phase11-plugin-scaffold): plugin scaffold only.
cmake_minimum_required(VERSION 3.20)

# ==============================================================================
# Version + Generated Files (see cmake/KratePlugin.cmake)
# ==============================================================================
krate_plugin_read_version(VORAGO)
krate_plugin_configure_generated_files()

# ==============================================================================
# Plugin Target
# ==============================================================================
set(PLUGIN_NAME "${VORAGO_NAME}")

# This list is ENUMERATED, never globbed (FR-003): a plugin .cpp that is missing
# here still compiles inside vorago_tests (which lists them a second time) and
# fails ONLY at the plugin link step. Every new .cpp must appear in BOTH lists.
smtg_add_vst3plugin(${PLUGIN_NAME}
    # Plugin Entry
    src/entry.cpp

    # Shared
    src/plugin_ids.h
    src/version.h

    # Processor (Audio Thread)
    src/processor/processor.h
    src/processor/processor.cpp

    # Controller (UI Thread)
    src/controller/controller.h
    src/controller/controller.cpp

    # Parameter Packs
    src/parameters/global_params.h
    src/parameters/macro_params.h

    # Prepare-time engine/reverb configuration (no DSP)
    src/engine/vorago_engine_config.h

    # Adapters to the shared library
    src/preset/vorago_preset_config.h
    src/update/vorago_update_config.h

    # VST3 SDK Common Sources (for MemoryStream used by preset saving)
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp
)

# Link SDK, VSTGUI, and KrateDSP
target_link_libraries(${PLUGIN_NAME}
    PRIVATE
        sdk
        vstgui_support
        KrateDSP
        KratePluginsShared
)

# Include directories
target_include_directories(${PLUGIN_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Configure version file
smtg_target_configure_version_file(${PLUGIN_NAME})

# ==============================================================================
# Platform-Specific Configuration (macOS AU targets + Windows .rc)
# ==============================================================================
krate_plugin_platform_setup(${PLUGIN_NAME}
    TAG VORAGO
    BUNDLE_BASE com.krateaudio.vorago
    ENTITLEMENTS Vorago.entitlements
    KIND instrument
)

# ==============================================================================
# VSTGUI Resources
# ==============================================================================
smtg_target_add_plugin_resources(${PLUGIN_NAME}
    RESOURCES
        resources/editor.uidesc
)

# ==============================================================================
# Post-Build: Copy to VST3 System Folder (see cmake/KratePlugin.cmake)
# ==============================================================================
krate_plugin_install_to_system(${PLUGIN_NAME})

# ==============================================================================
# Post-Build: Install Factory Presets to ProgramData
# ==============================================================================
krate_plugin_install_presets(${PLUGIN_NAME})

# ==============================================================================
# Compiler Warnings (see cmake/KratePlugin.cmake)
# ==============================================================================
krate_plugin_set_warnings(${PLUGIN_NAME})

# C4459 (MSVC /W4 only), FR-006: `dsp/include/krate/dsp/systems/timevar_comb_bank.h:931`
# declares a function-local `constexpr float kPi` that shadows the namespace-scope
# `Krate::DSP::kPi` (`dsp/include/krate/dsp/core/math_constants.h`). Vorago reaches
# it through `vorago_voice.h:137` -> `continuous_body.h` -> the comb bank. GCC/Clang
# do not warn here (`-Wshadow` is not in the shared flag set), so this is
# MSVC-specific noise from a dsp/ defect that is out of scope for this phase (no
# file under dsp/ is modified, SC-027). Suppressed narrowly HERE, on this target
# only. Delete this block when the dsp/ shadow is removed (drop the local `kPi`
# and use the math_constants.h one) in its own change.
if(MSVC)
    target_compile_options(${PLUGIN_NAME} PRIVATE /wd4459)
endif()

# ==============================================================================
# Plugin Tests
# ==============================================================================
if(VSTWORK_BUILD_TESTS)
    add_subdirectory(tests)
endif()
