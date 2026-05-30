# ==============================================================================
# Iterum VST3 Plugin
# ==============================================================================
# Constitution Principle VII: Modern CMake with target-based configuration
# Links against shared KrateDSP library

# ==============================================================================
# Version + Generated Files (see cmake/KratePlugin.cmake)
# ==============================================================================
krate_plugin_read_version(ITERUM)
# Iterum's UI badge shows a bare "v<version>" (no plugin name prefix).
set(KRATE_PLUGIN_UI_VERSION "\"v\" VERSION_STR")
krate_plugin_configure_generated_files()

# ==============================================================================
# Plugin Target
# ==============================================================================
set(PLUGIN_NAME "${ITERUM_NAME}")

smtg_add_vst3plugin(${PLUGIN_NAME}
    # Plugin Entry
    src/entry.cpp

    # Processor (Audio Thread)
    src/processor/processor.h
    src/processor/processor.cpp

    # Controller (UI Thread)
    src/controller/controller.h
    src/controller/controller.cpp
    src/controller/custom_views.h
    src/controller/custom_views.cpp
    src/controller/delay_time_sync_controller.h
    src/controller/delay_time_sync_controller.cpp
    src/controller/visibility_controller.h

    # VST3 SDK Common Sources (for MemoryStream used by preset saving)
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp

    # Shared
    src/plugin_ids.h
    src/version.h
    src/delay_mode.h

    # Parameters
    src/parameters/bbd_params.h
    src/parameters/digital_params.h
    src/parameters/dropdown_mappings.h
    src/parameters/freeze_params.h
    src/parameters/granular_params.h
    src/parameters/multitap_params.h
    src/parameters/note_value_ui.h
    src/parameters/param_display.h
    src/parameters/pingpong_params.h
    src/parameters/reverse_params.h
    src/parameters/shimmer_params.h
    src/parameters/spectral_params.h
    src/parameters/tape_params.h

    # Preset Configuration (adapter to shared library)
    src/preset/iterum_preset_config.h

    # Custom VSTGUI Views
    src/ui/tap_pattern_editor.h
    src/ui/tap_pattern_editor.cpp
    src/ui/tap_pattern_editor_logic.h
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
    TAG ITERUM
    BUNDLE_BASE com.krateaudio.iterum
    ENTITLEMENTS Iterum.entitlements
    KIND effect
)

# ==============================================================================
# VSTGUI Resources
# ==============================================================================
smtg_target_add_plugin_resources(${PLUGIN_NAME}
    RESOURCES
        resources/editor.uidesc
)

# ==============================================================================
# Post-Build install + compiler warnings (see cmake/KratePlugin.cmake)
# ==============================================================================
krate_plugin_install_to_system(${PLUGIN_NAME})
krate_plugin_set_warnings(${PLUGIN_NAME})

# ==============================================================================
# Plugin Tests
# ==============================================================================
if(VSTWORK_BUILD_TESTS)
    add_subdirectory(tests)
endif()
