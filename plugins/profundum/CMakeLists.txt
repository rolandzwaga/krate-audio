# ==============================================================================
# Profundum VST3 Plugin
# ==============================================================================
# Constitution Principle VII: Modern CMake with target-based configuration
# Links against shared KrateDSP library (Profundum DSP = Phases 1-9 of dsp/).
# Phase 10 (specs/profundum-phase10-plugin-scaffold): plugin scaffold only.
cmake_minimum_required(VERSION 3.20)

# ==============================================================================
# Version + Generated Files (see cmake/KratePlugin.cmake)
# ==============================================================================
krate_plugin_read_version(PROFUNDUM)
krate_plugin_configure_generated_files()

# ==============================================================================
# Plugin Target
# ==============================================================================
set(PLUGIN_NAME "${PROFUNDUM_NAME}")

# This list is ENUMERATED, never globbed (FR-003): a plugin .cpp that is missing
# here still compiles inside profundum_tests (which lists them a second time) and
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
    src/processor/spectrum_frame.h
    src/processor/spectrum_producer.h
    src/processor/goertzel_bank_simd.h
    src/processor/goertzel_bank_simd.cpp

    # Controller (UI Thread)
    src/controller/controller.h
    src/controller/controller.cpp

    # UI (Phase 12)
    src/ui/harmonic_bar_view.h
    src/ui/harmonic_bar_view.cpp
    src/ui/spread_curve_view.h
    src/ui/spread_curve_view.cpp
    src/ui/offset_ring_knob.h
    src/ui/offset_ring_knob.cpp
    src/ui/morph_indicator_slider.h
    src/ui/morph_indicator_slider.cpp
    src/ui/panel_sub_controller.h
    src/ui/panel_sub_controller.cpp
    src/ui/macro_ui_map.h
    src/ui/endpoint_descriptor.h

    # Parameter Packs
    src/parameters/global_params.h
    src/parameters/macro_params.h
    src/parameters/sound_param_row.h
    src/parameters/sound_param_table.h
    src/parameters/core_params.h
    src/parameters/anchor_params.h
    src/parameters/note_params.h
    src/parameters/fold_shape_params.h
    src/parameters/character_params.h
    src/parameters/dynamics_params.h
    src/parameters/field_params.h
    src/parameters/motion_params.h
    src/parameters/morph_params.h
    src/parameters/morph_mod_params.h
    src/parameters/performance_params.h
    src/parameters/endpoint_names.h

    # Prepare-time engine configuration (no DSP)
    src/engine/profundum_engine_config.h

    # Adapters to the shared library
    src/preset/profundum_preset_config.h
    src/update/profundum_update_config.h

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
        hwy  # T040: the plugin-local Goertzel kernel (goertzel_bank_simd.cpp)
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
    TAG PROFUNDUM
    BUNDLE_BASE com.krateaudio.profundum
    ENTITLEMENTS Profundum.entitlements
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
# No warning is suppressed on this target (FR-006).
krate_plugin_set_warnings(${PLUGIN_NAME})

# ==============================================================================
# Plugin Tests
# ==============================================================================
if(VSTWORK_BUILD_TESTS)
    add_subdirectory(tests)
endif()
